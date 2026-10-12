# Ryujinx GDB stub 最小客户端（读写内存、读寄存器、软件断点、继续 / 冻结）。
# 协议细节来自实测：stub 一次只服务一个客户端；
# 0x03 冻结全部客体线程、回停止包；c 先回 OK，命中断点时再异步来停止包。
# Minimal client for the Ryujinx GDB stub (read/write memory, read registers, software breakpoints, continue / freeze).
# Protocol details come from measurement: the stub serves only one client at a time;
# 0x03 freezes all guest threads and replies with a stop packet; c replies OK first, then a stop packet arrives asynchronously when a breakpoint is hit.
import socket, re

class Gdb:
    def __init__(self, host='127.0.0.1', port=55571, timeout=30):
        self.sock = socket.create_connection((host, port), timeout=timeout)
        self.sock.settimeout(timeout)
        self.buf = b''
        self.pending = []

    def close(self):
        try: self.sock.close()
        except OSError: pass

    def _send(self, payload):
        self.sock.sendall(b'$' + payload + b'#' + b'%02x' % (sum(payload) & 0xFF))

    def _recv_packet(self):
        while True:
            while b'#' not in self.buf or len(self.buf.split(b'#', 1)[1]) < 2:
                chunk = self.sock.recv(1 << 16)
                if not chunk: raise IOError('gdb stub 断开了连接')
                self.buf += chunk
            if self.buf[:1] in (b'+', b'-'): self.buf = self.buf[1:]; continue
            if not self.buf.startswith(b'$'):
                i = self.buf.find(b'$')
                if i < 0: self.buf = b''; continue
                self.buf = self.buf[i:]
            body, rest = self.buf[1:].split(b'#', 1)
            self.buf = rest[2:]
            self.sock.sendall(b'+')
            return body

    def cmd(self, payload):
        self._send(payload)
        expect_stop = payload[:1] in (b'?', b's', b'c') or payload.startswith(b'vCont')
        while True:
            body = self._recv_packet()
            if body.startswith(b'O') and len(body) > 1 and not body.startswith(b'OK'): continue   # 控制台输出包 | Console output packet
            if not expect_stop and body[:1] in (b'T', b'S') and b'thread:' in body:
                self._stash(body); continue               # 别的线程撞断点的异步停止包：暂存给 wait_stop | Asynchronous stop packet from another thread hitting a breakpoint: stash it for wait_stop
            return body

    def monitor(self, text):
        r = self.cmd(b'qRcmd,' + text.encode().hex().encode())
        if r in (b'OK', b''): return ''
        try: return bytes.fromhex(r.decode()).decode('utf-8', 'replace')
        except ValueError: return r.decode('utf-8', 'replace')

    def read(self, addr, n):
        r = self.cmd(b'm%x,%x' % (addr, n))
        if r.startswith(b'E'): raise IOError(f'读 {addr:#x}+{n:#x} 失败：{r.decode()}')
        return bytes.fromhex(r.decode())

    def write(self, addr, data):
        return self.cmd(b'M%x,%x:%s' % (addr, len(data), data.hex().encode())) == b'OK'

    def reg(self, n):
        """读通用寄存器 xN（GDB aarch64 编号：0–30 = x0–x30，31 = sp，32 = pc）。 | Read general-purpose register xN (GDB aarch64 numbering: 0–30 = x0–x30, 31 = sp, 32 = pc)."""
        r = self.cmd(b'p%x' % n)
        try: return int.from_bytes(bytes.fromhex(r.decode()), 'little')
        except ValueError: raise IOError(f'读寄存器 {n} 回包不是十六进制：{r[:40]!r}')

    def set_reg(self, n, v):
        return self.cmd(b'P%x=%s' % (n, (v & 0xFFFFFFFFFFFFFFFF).to_bytes(8, 'little').hex().encode())) == b'OK'

    def set_watch(self, addr, n=8):
        """写入断点（Z2）；Ryujinx 在 macOS hypervisor 下由 HvDebugState 实现。 | Write breakpoint (Z2); under the macOS hypervisor Ryujinx implements it with HvDebugState."""
        return self.cmd(b'Z2,%x,%x' % (addr, n)) == b'OK'
    def clear_watch(self, addr, n=8): return self.cmd(b'z2,%x,%x' % (addr, n)) == b'OK'

    def set_bp(self, addr): return self.cmd(b'Z0,%x,4' % addr) == b'OK'
    def clear_bp(self, addr): return self.cmd(b'z0,%x,4' % addr) == b'OK'

    def interrupt(self):
        self.sock.sendall(b'\x03')
        while True:
            b = self._recv_packet()
            if b[:1] in (b'T', b'S'): return b

    def cont(self):
        """继续运行；读到 c 的即时回包 OK 为止（Ryujinx 先回 OK，多线程时别的线程的停止包可能抢在 OK 前到：暂存给 wait_stop）。 | Continue execution; read until the immediate OK reply to c (Ryujinx replies OK first; with multiple threads, another thread's stop packet may arrive before the OK: stash it for wait_stop)."""
        self._send(b'c')
        while True:
            b = self._recv_packet()
            if b[:1] in (b'T', b'S') and b'thread:' in b: self._stash(b); continue
            return b

    @staticmethod
    def _tid(stop):
        import re as _re
        m = _re.search(rb'thread:([0-9a-f]+);', stop or b'')
        return m.group(1) if m else None

    def _stash(self, b):
        """暂存别的线程的停止包，按线程去重：线程停在断点上、还没被处理，它每次随全体继续都会在同一断点再停一次
        （定根：游玩段拍号断档 / 命令包重复记录都是这个），同一线程只留第一条。
        Stash stop packets from other threads, deduplicated by thread: a thread that is stopped at a breakpoint and not yet handled stops again at the same breakpoint every time everything is continued
        (root cause found: the tick-number gaps in the gameplay segment and the duplicated command-packet records were both this); keep only the first one per thread."""
        t = self._tid(b)
        if t is None or all(self._tid(p) != t for p in self.pending): self.pending.append(b)

    def select(self, stop):
        """把寄存器读写的目标线程（Hg）切到该停止包的线程。Ryujinx 每收到一次断点就把 Hg 设成最新撞断点的线程，
        先到的停止包若不切线程，读到的是后到那个线程的寄存器。
        Switch the target thread for register reads/writes (Hg) to the thread of this stop packet. Every time Ryujinx receives a breakpoint it sets Hg to the thread that most recently hit a breakpoint;
        if the thread is not switched for a stop packet that arrived earlier, what gets read are the registers of the thread that arrived later."""
        t = self._tid(stop)
        if t: self.cmd(b'Hg' + t)

    def wait_stop(self, timeout=None, select=True):
        if self.pending:
            b = self.pending.pop(0)
        else:
            old = self.sock.gettimeout()
            if timeout is not None: self.sock.settimeout(timeout)
            try:
                while True:
                    b = self._recv_packet()
                    if b[:1] in (b'T', b'S'): break
            finally: self.sock.settimeout(old)
        if select: self.select(b)
        return b

    def _wait_raw(self, timeout):
        old = self.sock.gettimeout(); self.sock.settimeout(timeout)
        try:
            while True:
                b = self._recv_packet()
                if b[:1] in (b'T', b'S'): return b
        finally: self.sock.settimeout(old)

    def step_over(self, addr, stop_packet):
        """越过函数入口断点（不用单步：hypervisor 模式下 Ryujinx 的单步会超时，实测 E01 + "Failed to step"）。
        入口第一条须不是跳转（两处入口都是 sub sp）：撤入口断点 → 入口+4 下临时断点 → 继续 → 停在 +4 → 撤临时、重下入口。
        别的线程在此期间的停止包暂存（按线程去重）给 wait_stop。返回 True = 本线程停在 +4；False = 200 次内没等到。
        Step over a function-entry breakpoint (without single-stepping: in hypervisor mode Ryujinx's single step times out; measured: E01 + "Failed to step").
        The first instruction at the entry must not be a branch (both entries are sub sp): remove the entry breakpoint → set a temporary breakpoint at entry+4 → continue → stop at +4 → remove the temporary one and re-set the entry one.
        Stop packets from other threads during this time are stashed (deduplicated by thread) for wait_stop. Returns True = this thread stopped at +4; False = not seen within 200 tries."""
        tid = self._tid(stop_packet)
        self.clear_bp(addr); self.set_bp(addr + 4); self.cont()
        same = False
        for _ in range(200):                      # 别的线程先停：暂存（按线程去重），再继续，直到本线程停在 +4 | Another thread stopped first: stash it (deduplicated by thread) and continue, until this thread stops at +4
            st = self._wait_raw(10)
            if tid is not None and self._tid(st) == tid:
                self.select(st)
                if self.reg(32) == addr + 4: same = True; break
                self.cont(); continue             # 本线程的陈旧停止包（越过前那次继续里在原断点又停了一次）：丢掉，再继续 | A stale stop packet for this thread (it stopped once more at the original breakpoint during the continue before the step-over): discard it and continue again
            self._stash(st); self.cont()
        self.clear_bp(addr + 4); self.set_bp(addr)
        return same

    def modules(self):
        """monitor get info 的模块表 → {名字: (起, 止)}。 | Module table from monitor get info → {name: (start, end)}."""
        info = self.monitor('get info')
        return {n: (int(a, 16), int(b, 16)) for a, b, n in re.findall(r'0x([0-9a-fA-F]+)\s*-\s*0x([0-9a-fA-F]+)\s+(\S+)', info)}, info
