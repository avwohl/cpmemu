#!/usr/bin/perl
use strict;
use FileHandle;
use File::stat;
use Data::Dumper;

my $S_IFMT  = 0170000;
my $S_IFDIR = 0040000;
my $S_IFREG = 0100000;

my %undef_op;
my %opcodes_used;
my %labels;
my %end_function = (
  'ret' => 1,
  'end' => 1,
  'jmp' => 1
);

my $chunk = undef;

sub chunk_end {
  return unless ( defined($chunk) );
  my $location = $$chunk{'N'};
  my $lineno   = $$location{'#'};
  my $sname    = $$location{'f'};

  my $chunk_lables = $$chunk{'L'};
  my $text         = $$chunk{'T'};

#  print("**** $sname $lineno\n$text\n");

  foreach my $alabel_name ( keys %$chunk_lables ) {
    my $alabel     = $$chunk_lables{$alabel_name};
    my $label_text = $$alabel{'l'};
    my $label_kind = $$alabel{'k'};
#    print(": $label_text $label_kind\n");
  }
  $chunk = undef;
}

sub chunk_add_label {
  my $location   = shift;
  my $label      = shift;
  my $label_name = $$label{'l'};
  die "no current chunk for label=$label_name\n" unless ( defined($chunk) );
  my $chunk_labels = $$chunk{'L'};
  $$chunk_labels{$label_name} = $label;
}

sub chunk_add_line {
  my $location = shift;
  my $line     = shift;
  if ( !defined($chunk) ) {
    $chunk = {
      'N' => $location,
      'L' => {},
      'T' => ''
    };
  }
  $$chunk{'T'} .= "$line\n";
}

sub record_label {
  my $location   = shift;
  my $label      = shift;
  my $label_name = $$label{'l'};
  $labels{$label_name} = [] if ( !defined( $labels{$label_name} ) );
  my $uses = $labels{$label_name};
  push @$uses, $label;
  &chunk_add_label( $location, $label );
}

  # 0 none
  # 1 8bit reg left shift 3
  # 2 8bit reg no shift
  # 3 16bit reg left shift 4
  # 4 emit as byte
  # 5 emit as word

my %op_table=(
  'aci'  => 0xce40,
  'aci'  => 0xce40,
  'adc'  => 0x8820,
  'add'  => 0x8020,
  'adi'  => 0xc640,
  'ana'  => 0xa020,
  'ani'  => 0xe640,
  'call' => 0xcd50,
  'cma'  => 0x2f00,
  'cmc'  => 0x3f00,
  'cmp'  => 0xb820,
  'cpi'  => 0xfe40,
  'daa'  => 0x1b00,
  'dad'  => 0x0930,
  'dcr'  => 0x0510,
  'dcx'  => 0x0b30,
  'di'   => 0xf300,
  'ei'   => 0xfb00,
  'hlt'  => 0x4c00,
  'in'   => 0xdb04,
  'inr'  => 0x0410,
  'inx'  => 0x0330,
  'jmp'  => 0xc350,
  'lda'  => 0x3a50,
  'ldax' => 0x0a30,
  'lhld' => 0x2a50,
  'lxi'  => 0x0135,
  'mov'  => 0x4012,
  'mvi'  => 0x0614,
  'nop'  => 0x000,
  'ora'  => 0xb020,
  'ori'  => 0xf640,
  'out'  => 0xd304,
  'pchl' => 0xe900,
  'pop'  => 0xc130,
  'push' => 0xc530,
  'ral'  => 0x1100,
  'rar'  => 0x1f00,
  'ret'  => 0xc900,
  'rlc'  => 0x0700,
  'rrc'  => 0x0f00,
  'rst'  => 0xc710,
  'sbb'  => 0x9820,
  'sbi'  => 0xde40,
  'shld' => 0x2250,
  'sphl' => 0xf900,
  'sta'  => 0x2500,
  'sta'  => 0x3250,
  'stax' => 0x0230,
  'stc'  => 0x3700,
  'sub'  => 0x9020,
  'sui'  => 0xd640,
  'xchg' => 0xeb00,
  'xra'  => 0xa820,
  'xri'  => 0xee40,
  'xthl' => 0xe300,
    );

my %condition_codes=(
  0 => 'nz',
  1 => 'z',
  2 => 'nc',
  3 => 'c',
  4 => 'po',
  5 => 'pe',
  6 => 'p',
  7 => 'm'
    );

sub add_cond_code_opcodes {
  my $prefix=shift;
  my $opcode_base=shift;
  my $opcode_arg=shift;
  foreach my $cond (0..7) {
    my $opcode=$opcode_base | ($cond << 3);
    my $suffix=$condition_codes{$cond};
    my $opcode_text=$prefix . $suffix;
    die "unexpected cond opcode $opcode_text\n" if (defined($op_table{$opcode_text})); 
    my $op_val = ($opcode<<8) | $opcode_arg;
    $op_table{$opcode_text} = $op_val;
#    printf("added $opcode_text %2x arg $opcode_arg\n" ,$opcode);
  }
}

sub add_computed_opcodes {
  &add_cond_code_opcodes('j',0xc2,0x50);
  &add_cond_code_opcodes('c',0xc4,0x50);
  &add_cond_code_opcodes('r',0xc0,0x00);
}


sub scan_dir {
  my $func     = shift;
  my $dir_name = shift;
  my $ih       = new FileHandle;
  opendir( $ih, $dir_name ) || die "can't open $dir_name name rc=$!\n";
  my @adir = readdir $ih;
  closedir $ih;
  foreach my $short_name ( sort @adir ) {
    next if ( $short_name =~ /^\./ );
    my $long_name = "$dir_name/$short_name";
    my $st        = lstat($long_name) or die "can't stat $long_name rc=$!";
    my $mtime     = $st->mtime;
    if ( ( $st->mode & $S_IFMT ) == $S_IFREG ) {
      &$func( $long_name, $short_name );
    }
    elsif ( ( $st->mode & $S_IFMT ) == $S_IFDIR ) {
      &scan_dir( $func, $long_name );
    }
    else {
      die "can't handle special file $long_name\n";
    }
  }
}

my $print_file = sub {
  my $lname = shift;
  my $sname = shift;
  return unless ( $sname =~ /^(.*).mac$/ );
  my $prefix = $1;
  my $if     = new FileHandle;
  my $lineno = 0;
  open( $if, '<', $lname ) || die "open $lname failed -- $!";
  my $working           = 1;
  my $ignore_till_endif = 0;

  while ($working) {
    my $aline = <$if>;
    last unless ( defined($aline) );
    $lineno++;
    my $location = {
      '#' => $lineno,
      'f' => $sname
    };
    chomp $aline;
    $aline =~ s/\x0d//;
    $aline =~ s/\x0c//;

    # cpm ^z is end of file
    if ( $aline =~ /^(.*)\x1a/ ) {
      $aline   = $1;
      $working = 0;
    }

    &chunk_add_line( $location, $aline );

    my $rest  = $aline;
    my $label = undef;
    if ( $aline =~ /^([A-Za-z\.\$][A-Za-z0-9\.\$]*)(:?:?)(.*)$/ ) {
      $rest = $3;
      my $kind = $2;
      $kind  = 'g' if ( $kind eq '::' );
      $kind  = ':' if ( $kind eq ':' );
      $label = {
        'l' => $1,
        'k' => $2,
        'N' => $location
      };

      &record_label( $location, $label );
    }

    $rest = '' if ( $rest =~ /^\s+$/ );

    # ; starts comment but is also used in a string litter so quote is specially
    if ( $rest =~ /^(.*)?';'(.*)$/ ) {
      $rest = $1 . "\x01" . $2;
    }

    my $comment = '';
    if ( $rest =~ /^([^\;]*)(\;.*)$/ ) {
      $comment = $2;
      $rest    = $1;
    }

    $rest = '' if ( $rest =~ /^\s+$/ );

    #undo semicolon arg hack
    $rest =~ s/\x01/';'/;

    $rest = $1 if ( $rest =~ /^(.*)\s+$/ );

    if ( $rest =~ /^\s?([A-Za-z0-9\.]\w*)(.*)\w?(.*)$/ ) {
      my $opcode = $1;
      $opcode = lc $opcode;
      my $args = $2;
      $args = $1 if ( $args =~ /^\s+(.*)$/ );
      $args = $1 if ( $args =~ /^(.*)\s+$/ );
      if ( $opcode eq 'if2' ) {
        $ignore_till_endif = 1;
        next;
      }
      if ( $opcode eq 'endif' ) {
        $ignore_till_endif = 0;
        next;
      }
      if ($ignore_till_endif) {
        next;
      }
      next if ( $opcode =~ /^\./ );
      if ( ( $opcode eq 'set' )
        && defined($label) )
      {
        $$label{'k'} = 's';
      }
      if ( $opcode eq 'end' ) {
        last;
      }

      #	print("x $rest\n");
      if ( defined( $end_function{$opcode} ) ) {
        &chunk_end();
        next;
      }

      if(!defined($op_table{$opcode})) {
        $undef_op{$opcode}++;
      }

      $opcodes_used{$opcode}++;

      #	print("opcode='$opcode' args='$args'\n");
      next;
    }

    next if ( length($rest) == 0 );

  }
  close($if) || die "close $lname failed -- $!";
  &chunk_end();
};

sub opcode_report {
  foreach my $op ( sort keys %opcodes_used ) {
    my $num_seen = $opcodes_used{$op};
    printf( "%5d $op\n", $num_seen );
  }
}

sub unop_report {
  foreach my $op ( sort keys %undef_op ) {
    my $num_seen = $undef_op{$op};
    printf( "%5d $op\n", $num_seen );
  }
}

sub dump_op_table {
  foreach my $op_text (sort keys %op_table) {
    my $op_val=$op_table{$op_text};
    print("$op_text\n");
#    my ($opcode, $op_arg) = @{$op_val};
#    print("$op_text $op_val\n");
  }
}

sub export_opcodes {
  foreach my $op_text (sort keys %op_table) {
    my $op_val=$op_table{$op_text};
    my $hex_op_val=sprintf("%04x",$op_val);
    my $full_op=$hex_op_val . $op_text;
    print("\"$full_op\",");
  }
}

sub main {
  &add_computed_opcodes(); 
  &export_opcodes();
#print Dumper(\%op_table);
#  &dump_op_table();
#  &scan_dir( $print_file, '.' );
#  &unop_report();
#  &opcode_report();
}

&main();
