/*
 * GDB remote serial protocol stub and shadow call stack.  See gdbstub.h.
 *
 * The stub never writes to guest memory except for a G, M or X packet that
 * asks for it.  A breakpoint is an address in a set, tested before each
 * instruction, so a debugger session leaves the guest's 64K as the program
 * wrote it.
 */

#include "gdbstub.h"

#include <cstddef>
#include <cstdlib>
#include <cstring>

namespace gdb {

Stub* active_stub = nullptr;

// ---------------------------------------------------------------------------
// Shadow call stack
// ---------------------------------------------------------------------------

void ShadowStack::push(uint16_t ret, uint16_t slot, uint8_t kind) {
  if (entries_.size() >= MAX_ENTRIES) {
    entries_.pop_front();   // rule 5: the oldest entry goes first
    dropped_ = true;
  }
  Entry e;
  e.ret = ret;
  e.slot = slot;
  e.kind = kind;
  entries_.push_back(e);
}

// Rule 2 of D7.  The stack grows down, so an entry deeper in the shadow stack
// has a higher slot, and an entry whose slot is below SP is one the program
// has already given up (a longjmp, an Ada exception, a routine that popped its
// own return address).
//
// 1. The innermost entry whose slot is exactly SP is the call this RET
//    returns from.  It and every entry above it go.
// 2. Otherwise the innermost entry that has a slot below SP and the return
//    address the RET went to is the call: a callee that pops its own
//    arguments, as the uplm80 REENTRANT epilogue does, moves the return
//    address up before the RET, so SP is not the slot.  Rule 4 is the reason
//    this search does not drop anything first: SP above the slot is the normal
//    state in that epilogue, not a sign that the entry is stale.
// 3. Otherwise nothing is dropped and the stack is marked out of step.
void ShadowStack::ret(uint16_t sp, uint16_t target) {
  size_t i = entries_.size();
  while (i > 0) {
    const Entry& e = entries_[i - 1];
    if (e.slot == sp) {
      entries_.erase(entries_.begin() + (std::ptrdiff_t)(i - 1), entries_.end());
      if (entries_.empty()) out_of_step_ = false;
      return;
    }
    if (e.slot > sp) break;
    i--;
  }
  i = entries_.size();
  while (i > 0) {
    const Entry& e = entries_[i - 1];
    if (e.slot > sp) break;
    if (e.ret == target) {
      entries_.erase(entries_.begin() + (std::ptrdiff_t)(i - 1), entries_.end());
      if (entries_.empty()) out_of_step_ = false;
      return;
    }
    i--;
  }
  out_of_step_ = true;
}

void Tracker::step(Target& t, StepEvents& ev) {
  t.step(ev);
  if (ev.interrupted) return;

  if (ev.irq) stack_.push(ev.irq_ret, ev.exec_sp, Entry::IRQ);

  uint16_t pc = (uint16_t)t.read_reg(REG_PC);
  uint16_t sp = (uint16_t)t.read_reg(REG_SP);

  if (ev.trap) {
    // Rule 3: a trap returns with no RET instruction.
    stack_.ret(ev.exec_sp, pc);
    return;
  }

  uint16_t at = ev.exec_pc;
  uint8_t op = t.read_mem(at);
  if (op == 0xCD || (op & 0xC7) == 0xC4) {
    // CALL, or CALL cc.  Taken if it pushed.
    if (sp == (uint16_t)(ev.exec_sp - 2)) stack_.push((uint16_t)(at + 3), sp, Entry::CALL);
  } else if ((op & 0xC7) == 0xC7) {
    if (sp == (uint16_t)(ev.exec_sp - 2)) stack_.push((uint16_t)(at + 1), sp, Entry::RST);
  } else if (op == 0xC9 || (op & 0xC7) == 0xC0) {
    // RET, or RET cc.  Taken if it popped.
    if (sp == (uint16_t)(ev.exec_sp + 2)) stack_.ret(ev.exec_sp, pc);
  } else if (op == 0xED && t.is_z80()) {
    uint8_t op2 = t.read_mem((uint16_t)(at + 1));
    // RETN and RETI: ED 45, ED 4D and the six undocumented copies.
    if ((op2 & 0xC7) == 0x45 && sp == (uint16_t)(ev.exec_sp + 2)) stack_.ret(ev.exec_sp, pc);
  }
}

static void put_addr(std::string& s, uint16_t v) {
  char b[16];
  snprintf(b, sizeof b, "0x%04X", v);
  s += b;
}

std::string format_backtrace(const ShadowStack& s, uint16_t pc) {
  std::string out;
  char b[32];
  snprintf(b, sizeof b, "#0  ");
  out += b;
  put_addr(out, pc);
  out += "\n";
  int n = 1;
  const std::deque<Entry>& e = s.entries();
  for (size_t i = e.size(); i > 0; i--, n++) {
    snprintf(b, sizeof b, "#%d  ", n);
    out += b;
    put_addr(out, e[i - 1].ret);
    const char* kind = e[i - 1].kind == Entry::CALL ? "call"
                     : e[i - 1].kind == Entry::RST ? "rst" : "irq";
    out += "  [shadow ";
    out += kind;
    out += "]\n";
  }
  if (s.out_of_step())
    out += "(the shadow stack is out of step: some frames above can be wrong)\n";
  if (s.dropped())
    out += "(older frames dropped: the shadow stack keeps 4096 entries)\n";
  return out;
}

// ---------------------------------------------------------------------------
// Hex helpers
// ---------------------------------------------------------------------------

static const char HEXD[] = "0123456789abcdef";

static void put_hex8(std::string& s, unsigned v) {
  s += HEXD[(v >> 4) & 15];
  s += HEXD[v & 15];
}

static int hexval(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

// Parse hex digits at p, advancing it.  False if there are none.
static bool parse_hex(const char*& p, uint32_t& v) {
  int d = hexval(*p);
  if (d < 0) return false;
  v = 0;
  while ((d = hexval(*p)) >= 0) {
    v = (v << 4) | (uint32_t)d;
    p++;
  }
  return true;
}

static bool decode_hex(const char* p, size_t n, std::string& out) {
  out.clear();
  if (n % 2) return false;
  for (size_t i = 0; i < n; i += 2) {
    int a = hexval(p[i]), b = hexval(p[i + 1]);
    if (a < 0 || b < 0) return false;
    out += (char)((a << 4) | b);
  }
  return true;
}

static std::string encode_hex(const std::string& s) {
  std::string out;
  for (size_t i = 0; i < s.size(); i++) put_hex8(out, (unsigned char)s[i]);
  return out;
}

// ---------------------------------------------------------------------------
// target.xml: GDB's gdb/features/z80-cpu.xml in a target description, with the
// 16-bit pc that reg_bytes() explains.
// ---------------------------------------------------------------------------

static const char TARGET_XML[] =
  "<?xml version=\"1.0\"?>\n"
  "<!DOCTYPE target SYSTEM \"gdb-target.dtd\">\n"
  "<target version=\"1.0\">\n"
  "<architecture>z80</architecture>\n"
  "<feature name=\"org.gnu.gdb.z80.cpu\">\n"
  "  <flags id=\"af_flags\" size=\"2\">\n"
  "    <field name=\"C\" start=\"0\" end=\"0\"/>\n"
  "    <field name=\"N\" start=\"1\" end=\"1\"/>\n"
  "    <field name=\"P/V\" start=\"2\" end=\"2\"/>\n"
  "    <field name=\"F3\" start=\"3\" end=\"3\"/>\n"
  "    <field name=\"H\" start=\"4\" end=\"4\"/>\n"
  "    <field name=\"F5\" start=\"5\" end=\"5\"/>\n"
  "    <field name=\"Z\" start=\"6\" end=\"6\"/>\n"
  "    <field name=\"S\" start=\"7\" end=\"7\"/>\n"
  "  </flags>\n"
  "  <reg name=\"af\" bitsize=\"16\" type=\"af_flags\"/>\n"
  "  <reg name=\"bc\" bitsize=\"16\" type=\"uint16\"/>\n"
  "  <reg name=\"de\" bitsize=\"16\" type=\"data_ptr\"/>\n"
  "  <reg name=\"hl\" bitsize=\"16\" type=\"data_ptr\"/>\n"
  "  <reg name=\"sp\" bitsize=\"16\" type=\"data_ptr\" />\n"
  "  <reg name=\"pc\" bitsize=\"16\" type=\"code_ptr\" />\n"
  "  <reg name=\"ix\" bitsize=\"16\" type=\"data_ptr\"/>\n"
  "  <reg name=\"iy\" bitsize=\"16\" type=\"data_ptr\"/>\n"
  "  <reg name=\"af'\" bitsize=\"16\" type=\"af_flags\"/>\n"
  "  <reg name=\"bc'\" bitsize=\"16\" type=\"uint16\"/>\n"
  "  <reg name=\"de'\" bitsize=\"16\" type=\"data_ptr\"/>\n"
  "  <reg name=\"hl'\" bitsize=\"16\" type=\"data_ptr\"/>\n"
  "  <reg name=\"ir\" bitsize=\"16\" type=\"uint16\"/>\n"
  "</feature>\n"
  "</target>\n";

// Every register is 16 bits wide here, pc included.  GDB's own z80-cpu.xml says
// 32 bits for pc, but GDB 18.1 sizes a code_ptr register by the pointer size of
// the architecture (2 bytes for a Z80) and ignores the bitsize, so a 32-bit pc
// gave "Remote 'g' packet reply is too long (expected 26 bytes, got 28 bytes)"
// (measured).
static int reg_bytes(int index) { (void)index; return 2; }

// The packet size we offer, in bytes of packet payload.
static const unsigned PACKET_SIZE = 0x4000;

// ---------------------------------------------------------------------------
// Stub
// ---------------------------------------------------------------------------

Stub::Stub(Target& t, Tracker& tr)
    : t_(t), tr_(tr), listener_(platform::kNoSocket), sock_(platform::kNoSocket),
      attached_(false), killed_(false), exit_sent_(false),
      watch_count_(0), watch_addr_(0), watch_type_(0),
      catch_all_syscalls_(false), pending_syscall_return_(-1),
      last_syscall_(0), last_syscall_entry_(true) {}

Stub::~Stub() {
  platform::net_close(sock_);
  platform::net_close(listener_);
}

bool Stub::listen(int port, const char* port_file) {
  int bound = 0;
  listener_ = platform::net_listen_local(port, &bound);
  if (listener_ == platform::kNoSocket) return false;
  fprintf(stderr, "GDB stub listening on 127.0.0.1:%d\n", bound);
  if (port_file) {
    FILE* f = fopen(port_file, "w");
    if (!f) {
      fprintf(stderr, "gdb stub: cannot write %s\n", port_file);
      return false;
    }
    fprintf(f, "%d\n", bound);
    fclose(f);
  }
  return true;
}

bool Stub::accept() {
  // The terminal is in raw mode, so ^C is a key and not a signal.  Watch the
  // console while waiting, so that ^C still ends a cpmemu that nothing
  // connects to.  Other keys typed meanwhile are lost.  A console that is not
  // a terminal (a pipe, a file, /dev/null) is not watched at all: no ^C can
  // come from it, and the guest needs its bytes after the connection.  At the
  // end of input the console is no longer watched either.
  bool watch_console = platform::is_terminal();
  while (watch_console) {
    int r = platform::wait_console_or_socket(listener_, -1);
    if (r < 0) {
      t_.check_signals();
      if (platform::termination_requested()) return false;
      continue;
    }
    if (r & 2) break;
    if (r & 1) {
      int c = platform::console_getchar();
      if (c == 0x03) {
        fprintf(stderr, "\n[Exiting: ^C while waiting for GDB]\n");
        return false;
      }
      if (c == -1) watch_console = false;
    }
  }
  sock_ = platform::net_accept(listener_);
  platform::net_close(listener_);
  listener_ = platform::kNoSocket;
  if (sock_ == platform::kNoSocket) return false;
  attached_ = true;
  fprintf(stderr, "GDB connected\n");
  return true;
}

void Stub::drop_connection() {
  platform::net_close(sock_);
  sock_ = platform::kNoSocket;
  attached_ = false;
}

bool Stub::fill() {
  char buf[4096];
  for (;;) {
    int n = platform::net_recv(sock_, buf, (int)sizeof buf, -1);
    if (n > 0) {
      inbuf_.append(buf, (size_t)n);
      return true;
    }
    if (n == -3) {
      t_.check_signals();
      continue;
    }
    return false;
  }
}

// Read until one packet or one ^C byte.  The payload is returned with the
// framing and the checksum taken off.  Acks and nacks that arrive between
// packets are handled here: a "-" repeats the last packet we sent.
int Stub::read_packet(std::string& out) {
  for (;;) {
    size_t i = 0;
    while (i < inbuf_.size()) {
      char c = inbuf_[i];
      if (c == '$') break;
      i++;
      if (c == 0x03) {
        inbuf_.erase(0, i);
        return 2;
      }
      if (c == '-' && !last_sent_.empty()) send_raw(last_sent_);
    }
    inbuf_.erase(0, i);
    if (inbuf_.empty()) {
      if (!fill()) return 0;
      continue;
    }
    size_t hash = inbuf_.find('#');
    if (hash == std::string::npos || inbuf_.size() < hash + 3) {
      if (!fill()) return 0;
      continue;
    }
    std::string body = inbuf_.substr(1, hash - 1);
    int c1 = hexval(inbuf_[hash + 1]), c2 = hexval(inbuf_[hash + 2]);
    inbuf_.erase(0, hash + 3);
    unsigned sum = 0;
    for (size_t k = 0; k < body.size(); k++) sum += (unsigned char)body[k];
    if (c1 < 0 || c2 < 0 || (sum & 0xFF) != (unsigned)((c1 << 4) | c2)) {
      send_raw("-");
      continue;
    }
    send_raw("+");
    out = body;
    return 1;
  }
}

void Stub::send_raw(const std::string& bytes) {
  if (sock_ == platform::kNoSocket) return;
  if (!platform::net_send_all(sock_, bytes.data(), (int)bytes.size())) drop_connection();
}

void Stub::send_packet(const std::string& payload) {
  unsigned sum = 0;
  std::string pkt = "$";
  for (size_t i = 0; i < payload.size(); i++) {
    unsigned char c = (unsigned char)payload[i];
    sum += c;
    pkt += (char)c;
  }
  pkt += '#';
  put_hex8(pkt, sum & 0xFF);
  last_sent_ = pkt;
  send_raw(pkt);
}

std::string Stub::registers_hex() {
  std::string out;
  for (int r = 0; r < REG_COUNT; r++) {
    uint32_t v = t_.read_reg(r);
    for (int b = 0; b < reg_bytes(r); b++) put_hex8(out, (v >> (8 * b)) & 0xFF);
  }
  return out;
}

static bool parse_reg_value(const char* p, size_t n, int r, uint32_t& v) {
  size_t need = (size_t)reg_bytes(r) * 2;
  if (n < need) return false;
  v = 0;
  for (int b = 0; b < reg_bytes(r); b++) {
    int hi = hexval(p[2 * b]), lo = hexval(p[2 * b + 1]);
    if (hi < 0 || lo < 0) return false;
    v |= (uint32_t)((hi << 4) | lo) << (8 * b);
  }
  return true;
}

bool Stub::set_registers(const std::string& hex) {
  size_t total = 0;
  for (int r = 0; r < REG_COUNT; r++) total += (size_t)reg_bytes(r) * 2;
  if (hex.size() != total) return false;
  size_t at = 0;
  for (int r = 0; r < REG_COUNT; r++) {
    uint32_t v;
    if (!parse_reg_value(hex.data() + at, hex.size() - at, r, v)) return false;
    t_.write_reg(r, v);
    at += (size_t)reg_bytes(r) * 2;
  }
  return true;
}

std::string Stub::monitor(const std::string& cmd) {
  size_t b = cmd.find_first_not_of(" \t\r\n");
  size_t e = cmd.find_last_not_of(" \t\r\n");
  std::string c = b == std::string::npos ? "" : cmd.substr(b, e - b + 1);
  if (c == "bt" || c == "backtrace") {
    return format_backtrace(tr_.stack(), (uint16_t)t_.read_reg(REG_PC));
  }
  return "monitor commands: bt\n";
}

bool Stub::syscall_wanted(int func) {
  if (func < 0) return false;
  return catch_all_syscalls_ || catch_syscalls_.count(func) != 0;
}

std::string Stub::stop_reply(StopKind k) {
  char b[64];
  switch (k) {
    case STOP_BREAK:   return "T05swbreak:;";
    case STOP_INT:     return "T02";
    case STOP_SYSCALL:
      snprintf(b, sizeof b, "T05syscall_%s:%x;",
               last_syscall_entry_ ? "entry" : "return", (unsigned)last_syscall_);
      return b;
    case STOP_WATCH: {
      snprintf(b, sizeof b, "T05%s:%x;",
               watch_type_ == 2 ? "watch" : watch_type_ == 3 ? "rwatch" : "awatch",
               (unsigned)watch_addr_);
      return b;
    }
    case STOP_STEP:
    case STOP_HALT:
    case STOP_DETACH:
    default:           return "T05";
  }
}

// Run the program.  mode is 'c' (continue), 's' (one step) or 'r' (step while
// pc is in [lo, hi)).  The checks come before the instruction runs, so a
// breakpoint at a BDOS entry stops before the trap pops the return address.
Stub::StopKind Stub::run(char mode, uint16_t lo, uint16_t hi) {
  bool first = true;          // the first instruction runs whatever is at pc
  unsigned n = 0;
  long irq_floor = -1;        // while running an interrupt routine to its end
  const bool catching = catch_all_syscalls_ || !catch_syscalls_.empty();
  bool skip_entry = pending_syscall_return_ >= 0;   // we were stopped at an entry

  // The return half of a syscall stop that we sat at: it is reported after the
  // trap has run, below.
  for (;;) {
    uint16_t pc = (uint16_t)t_.read_reg(REG_PC);
    if (!first && breaks_.count(pc)) return STOP_BREAK;

    int func = -1;
    if (catching) {
      func = t_.bdos_function_at(pc);
      if (syscall_wanted(func) && !skip_entry) {
        last_syscall_ = (uint32_t)func;
        last_syscall_entry_ = true;
        pending_syscall_return_ = func;
        return STOP_SYSCALL;
      }
    }
    skip_entry = false;

    size_t depth_before = tr_.stack().depth();
    StepEvents ev;
    tr_.step(t_, ev);
    first = false;

    if (ev.interrupted) return STOP_INT;
    if (ev.halt) return STOP_HALT;

    if (pending_syscall_return_ >= 0 && ev.trap) {
      last_syscall_ = (uint32_t)pending_syscall_return_;
      last_syscall_entry_ = false;
      pending_syscall_return_ = -1;
      return STOP_SYSCALL;
    }
    pending_syscall_return_ = -1;

    if (watch_count_ > 0) {
      uint16_t wa;
      int wt;
      if (t_.watch_hit(wa, wt)) {
        watch_addr_ = wa;
        watch_type_ = wt;
        return STOP_WATCH;
      }
    }

    if (mode != 'c') {
      // GDB's step expects to stay in the routine it is in, so an interrupt
      // routine is run to its end, and the instruction the interrupt came
      // before is then the one that gets stepped.
      if (ev.irq && irq_floor < 0) irq_floor = (long)depth_before;
      if (irq_floor >= 0) {
        if ((long)tr_.stack().depth() <= irq_floor) {
          irq_floor = -1;
          first = true;
        }
      } else if (mode == 's') {
        return STOP_STEP;
      } else {
        uint16_t now = (uint16_t)t_.read_reg(REG_PC);
        if (now < lo || now >= hi) return STOP_STEP;
      }
    }

    if ((++n & 0xFFF) == 0) {
      // Poll the debugger for ^C every 4096 instructions.
      char c;
      int r = platform::net_recv(sock_, &c, 1, 0);
      if (r == 1 && c == 0x03) return STOP_INT;
      if (r == 0 || r == -1) {
        drop_connection();
        return STOP_DETACH;
      }
      if (r == -3) t_.check_signals();
    }
  }
}

void Stub::resume(char mode, uint16_t lo, uint16_t hi) {
  StopKind k = run(mode, lo, hi);
  if (k == STOP_DETACH) return;
  send_packet(stop_reply(k));
}

static bool split_args(const char* p, uint32_t& a, uint32_t& b, const char*& rest) {
  if (!parse_hex(p, a)) return false;
  if (*p != ',') return false;
  p++;
  if (!parse_hex(p, b)) return false;
  rest = p;
  return true;
}

// One packet.  resumed is set if the packet ran the program (the stop reply
// has been sent by then), and detached if the debugger let go of it.
bool Stub::handle(const std::string& pkt, bool& resumed, bool& detached) {
  resumed = false;
  detached = false;
  if (pkt.empty()) { send_packet(""); return true; }
  const char* p = pkt.c_str() + 1;
  uint32_t a, b;
  const char* rest;

  switch (pkt[0]) {
    case '?':
      send_packet("T05");
      return true;

    case 'g':
      send_packet(registers_hex());
      return true;

    case 'G':
      send_packet(set_registers(pkt.substr(1)) ? "OK" : "E01");
      return true;

    case 'p': {
      if (!parse_hex(p, a) || a >= REG_COUNT) { send_packet("E01"); return true; }
      std::string out;
      uint32_t v = t_.read_reg((int)a);
      for (int i = 0; i < reg_bytes((int)a); i++) put_hex8(out, (v >> (8 * i)) & 0xFF);
      send_packet(out);
      return true;
    }

    case 'P': {
      uint32_t v;
      if (!parse_hex(p, a) || a >= REG_COUNT || *p != '=') { send_packet("E01"); return true; }
      p++;
      if (!parse_reg_value(p, strlen(p), (int)a, v)) { send_packet("E01"); return true; }
      t_.write_reg((int)a, v);
      send_packet("OK");
      return true;
    }

    case 'm': {
      if (!split_args(p, a, b, rest) || a >= 0x10000) { send_packet("E01"); return true; }
      // A read that runs past the top of memory is cut short, and a reply
      // shorter than asked for is legal.
      if (b > PACKET_SIZE / 2 - 8) b = PACKET_SIZE / 2 - 8;
      if (a + b > 0x10000) b = 0x10000 - a;
      std::string out;
      for (uint32_t i = 0; i < b; i++) put_hex8(out, t_.read_mem((uint16_t)(a + i)));
      send_packet(out);
      return true;
    }

    case 'M': {
      if (!split_args(p, a, b, rest) || *rest != ':' || a >= 0x10000 || a + b > 0x10000) {
        send_packet("E01");
        return true;
      }
      std::string data;
      const char* h = rest + 1;
      if (!decode_hex(h, strlen(h), data) || data.size() != b) { send_packet("E01"); return true; }
      for (uint32_t i = 0; i < b; i++) t_.write_mem((uint16_t)(a + i), (uint8_t)data[i]);
      send_packet("OK");
      return true;
    }

    case 'X': {
      // The data is binary: } escapes a byte, which is then xor 20H.
      size_t colon = pkt.find(':');
      if (colon == std::string::npos || !split_args(p, a, b, rest) || *rest != ':' ||
          a >= 0x10000 || a + b > 0x10000) {
        send_packet("E01");
        return true;
      }
      std::string data;
      for (size_t i = colon + 1; i < pkt.size(); i++) {
        if (pkt[i] == '}' && i + 1 < pkt.size()) {
          data += (char)(pkt[i + 1] ^ 0x20);
          i++;
        } else {
          data += pkt[i];
        }
      }
      if (data.size() != b) { send_packet("E01"); return true; }
      for (uint32_t i = 0; i < b; i++) t_.write_mem((uint16_t)(a + i), (uint8_t)data[i]);
      send_packet("OK");
      return true;
    }

    case 'H':
      send_packet("OK");
      return true;

    case 'T':
      send_packet("OK");
      return true;

    case 'c':
    case 's': {
      if (parse_hex(p, a)) t_.write_reg(REG_PC, a & 0xFFFF);
      resume(pkt[0], 0, 0);
      resumed = true;
      return true;
    }

    case 'k':
      killed_ = true;
      drop_connection();
      detached = true;
      return true;

    case 'D':
      send_packet("OK");
      drop_connection();
      detached = true;
      return true;

    case 'Z':
    case 'z': {
      bool set = pkt[0] == 'Z';
      uint32_t type;
      if (!parse_hex(p, type) || *p != ',') { send_packet(""); return true; }
      p++;
      if (type > 4) { send_packet(""); return true; }
      if (!parse_hex(p, a) || a >= 0x10000) { send_packet("E01"); return true; }
      if (type <= 1) {
        if (set) breaks_.insert((uint16_t)a); else breaks_.erase((uint16_t)a);
        send_packet("OK");
        return true;
      }
      // Z2, Z3 and Z4: a watchpoint of length b at a.
      if (*p != ',') { send_packet("E01"); return true; }
      p++;
      if (!parse_hex(p, b) || b == 0 || a + b > 0x10000) { send_packet("E01"); return true; }
      if (!t_.watch_set((int)type, (uint16_t)a, (uint16_t)b, set)) { send_packet(""); return true; }
      watch_count_ += set ? 1 : -1;
      if (watch_count_ < 0) watch_count_ = 0;
      send_packet("OK");
      return true;
    }

    default:
      break;
  }

  // The multi-letter packets.
  if (pkt == "vMustReplyEmpty") { send_packet(""); return true; }
  if (pkt == "vCont?") { send_packet("vCont;c;C;s;S;r"); return true; }
  if (pkt.compare(0, 6, "vCont;") == 0) {
    // One thread, so the first action decides.
    const char* q = pkt.c_str() + 6;
    char act = *q;
    if (act == 'C') act = 'c';
    if (act == 'S') act = 's';
    if (act == 'c' || act == 's') {
      resume(act, 0, 0);
      resumed = true;
      return true;
    }
    if (act == 'r') {
      q++;
      uint32_t lo, hi;
      if (!parse_hex(q, lo) || *q != ',') { send_packet("E01"); return true; }
      q++;
      if (!parse_hex(q, hi)) { send_packet("E01"); return true; }
      resume('r', (uint16_t)lo, hi > 0xFFFF ? 0xFFFF : (uint16_t)hi);
      resumed = true;
      return true;
    }
    send_packet("E01");
    return true;
  }
  if (pkt.compare(0, 10, "qSupported") == 0) {
    char b[160];
    snprintf(b, sizeof b,
             "PacketSize=%x;qXfer:features:read+;swbreak+;vContSupported+;QCatchSyscalls+",
             PACKET_SIZE);
    send_packet(b);
    return true;
  }
  if (pkt == "qAttached") { send_packet("1"); return true; }
  if (pkt == "qC") { send_packet("QC1"); return true; }
  if (pkt == "qfThreadInfo") { send_packet("m1"); return true; }
  if (pkt == "qsThreadInfo") { send_packet("l"); return true; }
  if (pkt.compare(0, 30, "qXfer:features:read:target.xml") == 0) {
    // qXfer:features:read:target.xml:OFFSET,LENGTH
    const char* q = pkt.c_str() + 30;
    uint32_t off, len;
    if (*q != ':') { send_packet("E01"); return true; }
    q++;
    if (!parse_hex(q, off) || *q != ',') { send_packet("E01"); return true; }
    q++;
    if (!parse_hex(q, len)) { send_packet("E01"); return true; }
    size_t total = strlen(TARGET_XML);
    if (off >= total) { send_packet("l"); return true; }
    size_t n = total - off;
    bool more = false;
    if (n > len) { n = len; more = true; }
    if (n > PACKET_SIZE - 16) { n = PACKET_SIZE - 16; more = true; }
    std::string out = more ? "m" : "l";
    // The XML holds none of the bytes that need escaping in a packet.
    out.append(TARGET_XML + off, n);
    send_packet(out);
    return true;
  }
  if (pkt.compare(0, 6, "qRcmd,") == 0) {
    std::string cmd;
    if (!decode_hex(pkt.c_str() + 6, pkt.size() - 6, cmd)) { send_packet("E01"); return true; }
    send_packet(encode_hex(monitor(cmd)));
    return true;
  }
  if (pkt.compare(0, 14, "QCatchSyscalls") == 0) {
    // QCatchSyscalls:0 turns it off.  QCatchSyscalls:1 catches every call and
    // QCatchSyscalls:1;n;n catches those numbers (hex).
    const char* q = pkt.c_str() + 14;
    if (*q != ':') { send_packet(""); return true; }
    q++;
    if (*q == '0') {
      catch_all_syscalls_ = false;
      catch_syscalls_.clear();
      send_packet("OK");
      return true;
    }
    if (*q != '1') { send_packet("E01"); return true; }
    q++;
    catch_syscalls_.clear();
    catch_all_syscalls_ = (*q == 0);
    while (*q == ';') {
      q++;
      uint32_t n;
      if (!parse_hex(q, n)) { send_packet("E01"); return true; }
      catch_syscalls_.insert((int)n);
    }
    send_packet("OK");
    return true;
  }

  // An empty reply means "not supported", which GDB requires for each
  // packet that the stub does not know.
  send_packet("");
  return true;
}

void Stub::serve() {
  while (attached_) {
    std::string pkt;
    int r = read_packet(pkt);
    if (r == 0) {
      // The debugger went away without saying so: let the program run on.
      drop_connection();
      return;
    }
    if (r == 2) continue;   // ^C with the program stopped means nothing
    bool resumed, detached;
    handle(pkt, resumed, detached);
    if (detached) return;
  }
}

void Stub::wait_console() {
  for (;;) {
    int r = platform::wait_console_or_socket(sock_, -1);
    if (r < 0) {
      t_.check_signals();
      if (platform::termination_requested()) return;
      continue;
    }
    if (r & 2) {
      char c;
      int n = platform::net_recv(sock_, &c, 1, 0);
      if (n == 1 && c == 0x03) throw Interrupt();
      if (n == 0 || n == -1) {
        drop_connection();   // the debugger is gone: the guest runs on
        return;
      }
    }
    if (r & 1) return;
  }
}

// Closing a socket that still holds unread input can reset the connection, and
// a reset can throw away the W packet we have just sent.  Wait, for a second at
// most, for the debugger to close its end first.
void Stub::finish_connection() {
  char buf[256];
  for (int waited = 0; waited < 1000; waited += 50) {
    int n = platform::net_recv(sock_, buf, (int)sizeof buf, 50);
    if (n == 0 || n == -1) break;
  }
  drop_connection();
}

void Stub::report_exit(int code) {
  if (!attached_ || exit_sent_ || killed_) return;
  exit_sent_ = true;
  std::string p = "W";
  put_hex8(p, (unsigned)code & 0xFF);
  send_packet(p);
  finish_connection();
}

void Stub::report_signal(int sig) {
  if (!attached_ || exit_sent_ || killed_) return;
  exit_sent_ = true;
  std::string p = "X";
  put_hex8(p, (unsigned)sig & 0xFF);
  send_packet(p);
  finish_connection();
}

}  // namespace gdb
