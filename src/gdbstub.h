/*
 * A GDB remote serial protocol (RSP) stub for cpmemu, and the shadow call
 * stack that "monitor bt" and --backtrace read.
 *
 * The stub talks to the emulator through the small Target interface below:
 * read and write a register, read and write a byte of memory, run one
 * instruction.  Nothing here includes qkz80, and nothing in qkz80 knows about
 * the stub, so the CPU core stays as the sibling projects compile it.  The
 * design is in docs/debugger_plan.md, and the user's view is in
 * docs/gdb_stub.md.
 */

#ifndef CPMEMU_GDBSTUB_H
#define CPMEMU_GDBSTUB_H

#include <cstdint>
#include <cstdio>
#include <deque>
#include <set>
#include <string>

#include "os/platform.h"

namespace gdb {

// The register numbers of GDB's z80-cpu.xml, in the order GDB sends them.
// All are 16 bits wide on the wire (see reg_bytes() in gdbstub.cc).
enum RegIndex {
  REG_AF, REG_BC, REG_DE, REG_HL, REG_SP, REG_PC, REG_IX, REG_IY,
  REG_AF2, REG_BC2, REG_DE2, REG_HL2, REG_IR,
  REG_COUNT
};

// What one Target::step() did, besides running an instruction.
struct StepEvents {
  bool irq;           // an interrupt was taken before the instruction
  uint16_t irq_ret;   // the address the interrupt pushed
  uint16_t exec_pc;   // the address of the instruction or trap that ran
  uint16_t exec_sp;   // SP when it started, after the interrupt's push
  bool trap;          // that was a BDOS or BIOS trap, not an instruction
  bool halt;          // a HLT that nothing can end, with a debugger attached
  bool interrupted;   // ^C arrived while a console read waited; nothing ran
  StepEvents()
      : irq(false), irq_ret(0), exec_pc(0), exec_sp(0), trap(false),
        halt(false), interrupted(false) {}
};

// Thrown by Stub::wait_console() when the debugger sends ^C while the guest
// waits for a key.  Target::step() catches it and sets StepEvents::interrupted.
struct Interrupt {};

class Target {
 public:
  virtual ~Target() {}
  virtual uint32_t read_reg(int index) = 0;
  virtual void write_reg(int index, uint32_t value) = 0;
  virtual uint8_t read_mem(uint16_t addr) = 0;
  virtual void write_mem(uint16_t addr, uint8_t value) = 0;
  // Run one instruction, or one BDOS or BIOS trap (which is not an
  // instruction).  Takes a pending interrupt first.
  virtual void step(StepEvents& ev) = 0;
  // True in Z80 mode.  In 8080 mode ED is not a prefix.
  virtual bool is_z80() = 0;
  // Called while the stub waits for the debugger: act on a termination signal.
  virtual void check_signals() = 0;
  // BDOS function number if executing the trap at pc is a BDOS call, else -1.
  virtual int bdos_function_at(uint16_t pc) = 0;
  // Watchpoints: type 2 = write, 3 = read, 4 = access.  False if the target
  // cannot watch.  watch_hit() returns and clears the last access that hit.
  virtual bool watch_set(int type, uint16_t addr, uint16_t len, bool on) = 0;
  virtual bool watch_hit(uint16_t& addr, int& type) = 0;
};

// One call that the CPU took and has not yet returned from.
struct Entry {
  enum Kind { CALL, RST, IRQ };
  uint16_t ret;    // the return address
  uint16_t slot;   // where the return address sits on the stack (SP after the push)
  uint8_t kind;
};

// The shadow call stack of docs/debugger_plan.md D7, source 1.
class ShadowStack {
 public:
  static const size_t MAX_ENTRIES = 4096;
  ShadowStack() : out_of_step_(false), dropped_(false) {}
  void push(uint16_t ret, uint16_t slot, uint8_t kind);
  // A RET, RETI or trap return that was taken: sp is SP before it, target is
  // where it went.
  void ret(uint16_t sp, uint16_t target);
  size_t depth() const { return entries_.size(); }
  bool out_of_step() const { return out_of_step_; }
  bool dropped() const { return dropped_; }
  const std::deque<Entry>& entries() const { return entries_; }
 private:
  std::deque<Entry> entries_;
  bool out_of_step_;
  bool dropped_;
};

// Runs Target::step() and keeps a ShadowStack from what the step did.
class Tracker {
 public:
  void step(Target& t, StepEvents& ev);
  const ShadowStack& stack() const { return stack_; }
 private:
  ShadowStack stack_;
};

// The text of "monitor bt" and of the --backtrace report: frame 0 is pc, and
// each later frame is a return address from the shadow stack, innermost first.
std::string format_backtrace(const ShadowStack& s, uint16_t pc);

class Stub {
 public:
  Stub(Target& t, Tracker& tr);
  ~Stub();
  // Listen on 127.0.0.1:port (0 = the system chooses), say so on stderr and
  // write the port to port_file if it is not null.  False on failure.
  bool listen(int port, const char* port_file);
  // Wait for the debugger to connect.  False on failure.
  bool accept();
  // Answer packets until the debugger detaches or kills the program.  Returns
  // on detach or kill (killed() tells which).  A program exit ends the process from inside, through
  // report_exit().
  void serve();
  // True while a debugger is connected.
  bool attached() const { return attached_; }
  // Block until the console has input.  Throws Interrupt on ^C from the
  // debugger.  Installed as the console wait hook while attached.
  void wait_console();
  // The W packet (exit code) or the X packet (signal) for the end of the run.
  // Sent at most once.  Does nothing when no debugger is attached.
  void report_exit(int code);
  void report_signal(int sig);
  // The debugger asked to kill the program ("k"), so no W packet is wanted.
  bool killed() const { return killed_; }

 private:
  enum StopKind { STOP_BREAK, STOP_STEP, STOP_INT, STOP_HALT, STOP_SYSCALL,
                  STOP_WATCH, STOP_DETACH };

  Target& t_;
  Tracker& tr_;
  platform::socket_t listener_;
  platform::socket_t sock_;
  bool attached_;
  bool killed_;
  bool exit_sent_;
  std::string inbuf_;
  std::string last_sent_;
  std::set<uint16_t> breaks_;
  int watch_count_;
  uint16_t watch_addr_;
  int watch_type_;
  bool catch_all_syscalls_;
  std::set<int> catch_syscalls_;
  int pending_syscall_return_;

  bool fill();
  int read_packet(std::string& out);   // 1 packet, 2 ^C byte, 0 closed
  void send_packet(const std::string& payload);
  void send_raw(const std::string& bytes);
  void drop_connection();
  void finish_connection();
  bool handle(const std::string& p, bool& resumed, bool& detached);
  void resume(char mode, uint16_t lo, uint16_t hi);
  StopKind run(char mode, uint16_t lo, uint16_t hi);
  std::string stop_reply(StopKind k);
  std::string registers_hex();
  bool set_registers(const std::string& hex);
  std::string monitor(const std::string& cmd);
  bool syscall_wanted(int func);
  uint32_t last_syscall_;
  bool last_syscall_entry_;
};

// The stub of this process, if any.  The exit paths of cpmemu.cc call
// through it.
extern Stub* active_stub;

}  // namespace gdb

#endif  // CPMEMU_GDBSTUB_H
