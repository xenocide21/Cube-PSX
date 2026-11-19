#!/usr/bin/env python3
"""List every instruction in a PS1 ELF that the console's R3000 (MIPS I, no FPU) does not have.  usage: checkops.py elf"""
import subprocess, sys, collections
OK = set('''add addi addiu addu and andi b bal beq beqz bgez bgezal bgtz blez bltz bltzal bne bnez break div divu j jal jalr jr lb lbu
lh lhu li lui lw lwl lwr mfhi mflo mthi mtlo move mult multu neg negu nop nor not or ori sb sh sll sllv slt slti sltiu sltu sra srav
srl srlv sub subu sw swl swr syscall xor xori mfc0 mtc0 rfe mfc2 mtc2 cfc2 ctc2 lwc2 swc2 c2 cop2 c0'''.split())
out = subprocess.check_output(['mipsel-linux-gnu-objdump', '-d', '-M', 'no-aliases', sys.argv[1]]).decode()
bad = collections.Counter(); where = {}; fn = '?'
for line in out.splitlines():
    if line.endswith('>:'): fn = line.split('<')[1][:-2]
    parts = line.split('\t')
    if len(parts) >= 3 and parts[0].strip().endswith(':'):
        op = parts[2].split()[0]
        if op not in OK: bad[op] += 1; where.setdefault(op, fn)
if bad:
    for op, n in bad.most_common(): print('NOT MIPS I: %-8s x%d  (first in %s)' % (op, n, where[op]))
    sys.exit(1)
print('ok: every instruction is MIPS I / COP0 / GTE')
