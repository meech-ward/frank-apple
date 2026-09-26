#!/usr/bin/env python3
"""Compile the port's actual fast-path macros against a small memory-bus fixture.
Checks ROM write protection and side-effecting IIe ROM reads without a board.
Live ProDOS boot and application tests remain separate.
"""
from pathlib import Path
import subprocess,tempfile
source=(Path(__file__).resolve().parents[1]/'src/mii_65c02.c').read_text()
def macro(name):
 lines=source[source.index('#define '+name):].splitlines();out=[]
 for line in lines:
  out.append(line)
  if not line.endswith('\\'):break
 return '\n'.join(out)+'\n'
fixture=r'''
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>
#define likely(x) (x)
typedef struct { bool ro; uint8_t data[65536]; } mii_bank_t;
typedef struct { struct { uint8_t read,write; } mem[256]; mii_bank_t bank[2]; } mii_t;
typedef struct { uint16_t addr; uint8_t data,w; } state_t;
typedef struct cpu_t { void *access_param; unsigned cycle; state_t (*access)(struct cpu_t *,state_t); } cpu_t;
static unsigned io;
static state_t access_bus(cpu_t *cpu,state_t s){(void)cpu;io++;s.data=0x55;return s;}
static void _run_timers_inline(cpu_t *cpu){(void)cpu;}
static uint8_t mii_bank_peek(mii_bank_t *b,uint16_t a){return b->data[a];}
static void mii_bank_poke(mii_bank_t *b,uint16_t a,uint8_t v){b->data[a]=v;}
'''+macro('_IS_IO_ADDR')+macro('_FETCH')+macro('_STORE')+r'''
int main(void){
 static mii_t m;cpu_t storage={.access_param=&m,.access=access_bus};cpu_t *cpu=&storage;state_t s={0};
 m.bank[1].ro=true;m.mem[0xd0].write=m.mem[0xd0].read=1;m.bank[1].data[0xd000]=0xa5;
 _STORE(0xd000,0x12);assert(m.bank[1].data[0xd000]==0xa5);
 _FETCH(0xd000);assert(s.data==0xa5 && io==0);
 _STORE(0x300,0x42);_FETCH(0x300);assert(s.data==0x42 && io==0);
 _FETCH(0xc300);assert(io==1);_FETCH(0xc3ff);assert(io==2);
 _FETCH(0xcfff);assert(io==3);_FETCH(0xc083);assert(io==4);
 _FETCH(0xc600);assert(io==4); /* ordinary slot ROM still uses the fast path */
 _STORE(0xc300,0);assert(io==5);
 return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='badge-bus-') as d:
 p=Path(d);(p/'check.c').write_text(fixture)
 subprocess.run(['cc','-Wall','-Wextra','-Werror',str(p/'check.c'),'-o',str(p/'check')],check=True)
 subprocess.run([str(p/'check')],check=True)
print('PASS: ROM is read-only; RAM writes work; C0, C3, and CFFF use the side-effecting bus path.')
