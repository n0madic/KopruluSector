"""Structured exception handling for guest threads.

Windows x86 SEH is a linked list of registration records at FS:[0]
(``{next, handler}``). Dispatch builds an EXCEPTION_RECORD and a CONTEXT on the faulting
thread's stack, calls each handler ``handler(record, frame, context, dispatcher)`` and
acts on the result: 0 (ExceptionContinueExecution) reloads the registers from the
CONTEXT, 1 (ExceptionContinueSearch) tries the next record. Language handlers
(``_except_handler4``, ``__CxxFrameHandler3``) normally call RtlUnwind and then jump
into the ``__except`` block or catch funclet themselves, never returning to the
dispatcher; :func:`rtl_unwind` therefore discards HLE frames that lie inside the
unwound stack range.

Every dispatched exception is recorded in ``machine.services["exceptions"]``.
"""
from __future__ import annotations

import logging
import struct

from unicorn import x86_const as X

from .machine import Call, Frame, GuestFault

log = logging.getLogger(__name__)

CONTEXT_SIZE = 0x2CC
RECORD_SIZE = 0x50
CTX_REGS = [  # (offset, register)
    (0x8C, X.UC_X86_REG_GS), (0x90, X.UC_X86_REG_FS), (0x94, X.UC_X86_REG_ES), (0x98, X.UC_X86_REG_DS),
    (0x9C, X.UC_X86_REG_EDI), (0xA0, X.UC_X86_REG_ESI), (0xA4, X.UC_X86_REG_EBX), (0xA8, X.UC_X86_REG_EDX),
    (0xAC, X.UC_X86_REG_ECX), (0xB0, X.UC_X86_REG_EAX), (0xB4, X.UC_X86_REG_EBP), (0xB8, X.UC_X86_REG_EIP),
    (0xBC, X.UC_X86_REG_CS), (0xC0, X.UC_X86_REG_EFLAGS), (0xC4, X.UC_X86_REG_ESP), (0xC8, X.UC_X86_REG_SS),
]
EXCEPTION_NONCONTINUABLE = 1
EXCEPTION_UNWINDING = 2
EXCEPTION_EXIT_UNWIND = 4


def _write_context(m, ctx_addr, overrides):
    m.write(ctx_addr, bytes(CONTEXT_SIZE))
    m.w32(ctx_addr, 0x1003F)
    for off, reg in CTX_REGS:
        m.w32(ctx_addr + off, overrides.get(reg, m.reg(reg)))


def _load_context(m, ctx_addr):
    for off, reg in CTX_REGS:
        if reg in (X.UC_X86_REG_CS, X.UC_X86_REG_SS, X.UC_X86_REG_DS, X.UC_X86_REG_ES, X.UC_X86_REG_GS,
                   X.UC_X86_REG_FS):
            continue
        m.set_reg(reg, m.r32(ctx_addr + off))


def _dispatch(m, record, context, scratch_top):
    teb = m.current.teb
    node = m.r32(teb)
    stack_low, stack_high = m.current.stack_base, m.current.stack_top
    while node != 0xFFFFFFFF:
        if not (stack_low <= node < stack_high):
            raise GuestFault(f"SEH record {node:#x} outside the stack")
        handler = m.r32(node + 4)
        dispatcher = scratch_top - 0x10
        r = yield Call(handler, (record, node, context, dispatcher), stack=scratch_top - 0x20)
        if r == 0:
            return "continue"
        if r != 1:
            log.warning("SEH handler %#x returned %d", handler, r)
        node = m.r32(node)
    raise GuestFault({"kind": "unhandled exception", "code": hex(m.r32(record)), "address": hex(m.r32(record + 12))})


def _note(m, code, address, info):
    rec = {"code": hex(code), "address": hex(address), "info": [hex(i) for i in info], "thread": m.current.tid}
    m.services.setdefault("exceptions", []).append(rec)
    log.info("guest exception %s", rec)


def raise_software_exception(m, code, flags, n, args):
    """RaiseException: generator run inside the RaiseException API frame."""
    esp = m.esp  # points at the return address of RaiseException
    ret = m.r32(esp)
    info = list(struct.unpack(f"<{min(n, 15)}I", m.read(args, 4 * min(n, 15)))) if n and args else []
    _note(m, code, ret, info)
    base = (esp - 0x100 - CONTEXT_SIZE - RECORD_SIZE) & ~0xF
    context, record = base, base + CONTEXT_SIZE
    _write_context(m, context, {X.UC_X86_REG_EIP: ret, X.UC_X86_REG_ESP: esp + 4 + 16})
    m.write(record, struct.pack("<IIIII", code, flags & EXCEPTION_NONCONTINUABLE, 0, ret, len(info)) +
            b"".join(struct.pack("<I", i) for i in info).ljust(60, b"\0"))
    yield from _dispatch(m, record, context, base)
    # ExceptionContinueExecution: resume at the context (normally: return from RaiseException)
    _load_context(m, context)
    frame = m.current.frames[-1]
    frame.on_finish = lambda _v: None  # registers already loaded from the CONTEXT
    return 0


def dispatch_hardware_fault(m, code, info):
    """Start dispatching a CPU fault at the current EIP (called from the run loop)."""
    eip, esp = m.eip, m.esp
    _note(m, code, eip, info)
    base = (esp - 0x200 - CONTEXT_SIZE - RECORD_SIZE) & ~0xF
    context, record = base, base + CONTEXT_SIZE
    _write_context(m, context, {})
    m.write(record, struct.pack("<IIIII", code, 0, 0, eip, len(info)) +
            b"".join(struct.pack("<I", i) for i in info).ljust(60, b"\0"))
    m.set_reg(X.UC_X86_REG_ESP, base - 0x40)

    def gen():
        yield from _dispatch(m, record, context, base)
        return 0

    frame = Frame(gen(), None, base - 0x40, 0, on_finish=lambda _v: _load_context(m, context))
    m.current.frames.append(frame)
    m._advance(frame, None)


def rtl_unwind(m, target_frame, target_ip, record, value):
    """RtlUnwind: call handlers of the records above ``target_frame`` with the unwinding flag."""
    teb = m.current.teb
    esp = m.esp
    base = (esp - 0x100 - CONTEXT_SIZE - RECORD_SIZE) & ~0xF
    context, rec = base, base + CONTEXT_SIZE
    _write_context(m, context, {})
    if record:
        m.write(rec, m.read(record, RECORD_SIZE))
    else:
        m.write(rec, struct.pack("<IIIII", 0xC0000027, 0, 0, target_ip, 0).ljust(RECORD_SIZE, b"\0"))
    flags = m.r32(rec + 4) | EXCEPTION_UNWINDING | (EXCEPTION_EXIT_UNWIND if not target_frame else 0)
    m.w32(rec + 4, flags)
    node = m.r32(teb)
    while node != 0xFFFFFFFF and node != target_frame:
        handler = m.r32(node + 4)
        yield Call(handler, (rec, node, context, base - 0x10), stack=base - 0x20)
        node = m.r32(node)
        m.w32(teb, node)
    # Discard HLE frames that live inside the unwound region (e.g. the RaiseException frame).
    me = m.current.frames[-1]
    limit = target_frame if target_frame else m.current.stack_top
    m.current.frames[:] = [f for f in m.current.frames if f is me or f.esp >= limit]
    return value
