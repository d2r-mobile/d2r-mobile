#!/usr/bin/env python3
"""真机冒烟的日志通道：经 iproxy（USB 端口映射，不经 devicectl）连 App 的测试日志端口 DM_LOG_PORT。

实测 devicectl --console 首行延迟约 76 s、被杀的会话还拖住下一条 devicectl 约 80 s；文件通道（copy from）同样卡。
App 侧（dm/ios/Sources/main.m，只在 DM_LOG_PORT 开关下）：连上先补发 boot.txt 已写内容，再逐行实时转发；收到 quit 优雅退出。

用法：
  logport.py alive <本地端口>                      退出码 0 = App 在监听（连上即有数据），1 = 没有
  logport.py quit <本地端口> [上限秒=10]           让在监听的 App 退出，并按「端口不再有数据」确认已退；退出码 0 = 已无实例
  logport.py until <本地端口> <日志文件> <上限秒> <标记>   收日志到出现标记行为止（触摸玩家路径）
  logport.py player <本地端口> <日志文件> <主菜单上限秒> <进局秒> <暂停秒>   玩家路径（见 player()）
  logport.py read <本地端口> <日志文件> <主菜单上限秒> <撤屏上限秒>
      读日志判三阶段，成功后发 quit 并确认退出。标准输出 shell 可 eval 的结果：
      result=0|1 stage=启动|主菜单|进局|撤屏 pk=0|1（本局见到已知 PK）tL= tM= tG= tS=（Unix 整秒）exited=0|1 lines=N
      Log channel for real device smoke tests: connects to the App's test log port DM_LOG_PORT via iproxy (USB port mapping, not through devicectl).
      Measured: devicectl --console has a first-line delay of about 76 s, and a killed session holds up the next devicectl for about 80 s; the file channel (copy from) stalls the same way.
      App side (dm/ios/Sources/main.m, only under the DM_LOG_PORT switch): on connect it first replays what has already been written to boot.txt, then forwards line by line in real time; on receiving quit it exits gracefully.
      Usage:
        logport.py alive <local port>                      exit code 0 = App is listening (data arrives on connect), 1 = not
        logport.py quit <local port> [limit seconds=10]           make the listening App exit, and confirm it has exited by "no more data on the port"; exit code 0 = no instance left
        logport.py until <local port> <log file> <limit seconds> <marker>   collect the log until the marker line appears (touch player path)
        logport.py player <local port> <log file> <main menu limit seconds> <enter-game seconds> <pause seconds>   player path (see player())
        logport.py read <local port> <log file> <main menu limit seconds> <screen dismissal limit seconds>
            read the log and judge three stages, on success send quit and confirm exit. Stdout is a result the shell can eval:
            result=0|1 stage=启动|主菜单|进局|撤屏 (launch|main menu|enter game|screen dismissal) pk=0|1 (a known PK was seen this run) tL= tM= tG= tS= (Unix whole seconds) exited=0|1 lines=N
"""
import socket
import sys
import time

PK = '占不到 pthread 键 507'


def connect(port, wait=1.0):
    """连 iproxy 本地端口；App 在监听时一连上就有补发数据。返回 (套接字, 首块数据) 或 (None, b'')。 | Connect to the iproxy local port; when the App is listening, replayed data arrives as soon as connected. Returns (socket, first chunk of data) or (None, b'')."""
    try:
        s = socket.create_connection(('127.0.0.1', port), timeout=1)
    except OSError:
        return None, b''
    s.settimeout(wait)
    try:
        d = s.recv(65536)
    except (socket.timeout, OSError):
        d = b''
    if not d:
        s.close()
        return None, b''
    return s, d


def alive(port):
    s, _ = connect(port)
    if s:
        s.close()
        return True
    return False


ACK = 'DM_LOG_PORT：收到 quit，退出'.encode()


def quit_and_confirm(port, limit=10.0, sock=None, out=None):
    """确认式退出：发 quit 后保持连接继续读，见到 App 的同步回执才算送达（2 s 没见到就重发，quit 幂等），
    再读到 App 关连接（进程退出）为止，最后按「端口不再有数据」确认已退。
    不可发完即关 / 半关：iproxy 2.1.1 客户端一 EOF 就拆整条 usbmux 连接，quit 可能随拆除丢失（冒烟残留进程的根因）。
    Confirmed exit: after sending quit keep the connection open and keep reading; delivery only counts once the App's synchronous acknowledgment is seen (resend if not seen within 2 s; quit is idempotent),
        then read until the App closes the connection (process exit), and finally confirm it has exited by "no more data on the port".
        Do not close / half-close right after sending: the iproxy 2.1.1 client tears down the whole usbmux connection upon EOF, and quit may be lost with the teardown (root cause of leftover smoke-test processes)."""
    end = time.time() + limit
    if sock is None:
        sock, _ = connect(port)
    if sock is None:
        return True
    tail = b''; acked = False; resend = 0.0
    try:
        while time.time() < end:
            if not acked and time.time() >= resend:
                sock.sendall(b'quit\n'); resend = time.time() + 2.0
            sock.settimeout(0.2)
            try:
                d = sock.recv(65536)
            except socket.timeout:
                continue
            if not d:
                break                                   # App 关连接 = 进程在退 | App closed the connection = the process is exiting
            if out:
                out.write(d)
            tail = (tail + d)[-4096:]
            if ACK in tail:
                acked = True
    except OSError:
        pass
    sock.close()
    if out:
        out.flush()
    while time.time() < end:
        if not alive(port):
            return True
        time.sleep(0.3)
    return False


def read(port, log, menu_limit, game_limit):
    t0 = time.time()
    out = open(log, 'ab')
    r = dict(result=1, stage='启动', pk=0, tL='', tM='', tG='', tS='', exited=0, lines=0)
    deadline = t0 + menu_limit
    sock = None
    buf = b''
    while time.time() < deadline and r['result']:
        if sock is None:
            sock, first = connect(port)
            if sock is None:
                time.sleep(0.2)
                continue
            buf += first
        else:
            try:
                d = sock.recv(65536)
            except socket.timeout:
                d = None
            except OSError:
                d = b''
            if d == b'':
                sock.close()
                sock = None
                continue
            if d:
                buf += d
        while b'\n' in buf:
            raw, buf = buf.split(b'\n', 1)
            out.write(raw + b'\n')
            line = raw.decode(errors='replace')
            now = int(time.time())   # 绝对时刻（整秒），与 smoke.sh 的 tA=$(date +%s) 同口径 | Absolute time (whole seconds), same basis as smoke.sh's tA=$(date +%s)
            r['lines'] += 1
            if not r['tL']:
                r['tL'] = now
            if PK in line:
                r['pk'] = 1
            if not r['tM'] and 'MainMenuButtonRibbon' in line:
                r['tM'], r['stage'] = now, '进局'
                deadline = time.time() + game_limit
            if r['tM'] and not r['tG'] and 'FrontEndNavigation ToGame' in line:
                r['tG'], r['stage'] = now, '撤屏'
            if r['tG'] and not r['tS'] and 'UnloadPanel LoadScreenPanel' in line:
                r['tS'], r['result'] = now, 0
                break
    if r['tL'] and r['stage'] == '启动':
        r['stage'] = '主菜单'
    r['exited'] = int(quit_and_confirm(port, sock=sock, out=out))
    out.close()
    print(' '.join(f'{k}={v}' for k, v in r.items()))
    return r['result']


def snapshot_hang(log):
    """玩家路径红时留现场：对 App 进程取样（各线程调用栈）+ 记当时的前台应用，写到日志同目录的 player_sample_<pid>.txt / player_env.txt。
    三次「未打开暂停菜单」都是进局约 10 秒后不再出帧、主线程定时器照常，事后日志里读不出停在哪；重跑 10 局没复现。
    只留证据，不改红绿判定；取样失败不影响后续退出。
    When the player path fails, capture diagnostics: sample the App process (call stacks of each thread) + record the foreground app at that moment, written next to the log as player_sample_<pid>.txt / player_env.txt.
        All three 「未打开暂停菜单」(pause menu not opened) cases were: no more frames about 10 seconds after entering the game while the main thread timer ran as usual, and the log afterwards could not tell where it was stopped; 10 reruns did not reproduce it.
        Evidence only, the pass/fail verdict is not changed; a sampling failure does not affect the subsequent exit."""
    import os, subprocess
    d = os.path.dirname(os.path.abspath(log))
    try:
        ps = subprocess.run(['ps', '-axo', 'pid=,comm='], capture_output=True, text=True, timeout=5).stdout
        procs = [l.split(None, 1) for l in ps.splitlines() if l.rstrip().endswith('/Wrapper/DmIOS.app/DmIOS')]   # 同机可能还有别的线的包在跑（另一个设备族），都取、各存各的 | Packages from other lines may also be running on the same machine (another device family); take them all, saving each separately
        pids = [p[0] for p in procs]
        env = [f"时刻 {time.strftime('%H:%M:%S')}  同名进程 {len(procs)} 个"] + [f"  pid {p[0]}  {p[1].strip()}" for p in procs]
        for cmd in ("lsappinfo info -only name $(lsappinfo front)", "pmset -g assertions | grep -i display | head -4"):
            env.append(subprocess.run(cmd, shell=True, capture_output=True, text=True, timeout=5).stdout.strip())
        open(os.path.join(d, 'player_env.txt'), 'w').write('\n'.join(env) + '\n')
        for pid in pids: subprocess.run(['sample', pid, '2', '-file', os.path.join(d, f'player_sample_{pid}.txt')], capture_output=True, timeout=30)
    except Exception as e:
        try: open(os.path.join(d, 'player_env.txt'), 'a').write(f'留现场失败：{e}\n')
        except Exception: pass


def player(port, log, menu_limit, game_s, pause_s):
    """玩家路径：进局（读取画面撤下）后满 game_s 秒，由键序打开暂停菜单，再静置 pause_s 秒；期间日志口不断、无看门狗 / CRASH 行。
    标准输出：result=0|1 stage=启动|进局|暂停|静置 why=<原因> tS= tP= exited=0|1
    Player path: game_s seconds after entering the game (loading screen dismissed), the key sequence opens the pause menu, then idle for pause_s seconds; meanwhile the log port stays connected and there are no watchdog / CRASH lines.
        Stdout: result=0|1 stage=启动|进局|暂停|静置 (launch|enter game|pause|idle) why=<reason> tS= tP= exited=0|1"""
    t0 = time.time(); out = open(log, 'ab'); sock = None; buf = b''; tS = tP = None; why = ''
    end = t0 + menu_limit + game_s + 30
    while time.time() < end:
        if sock is None:
            sock, first = connect(port)
            if sock is None:
                if tS: why = '日志口断开（App 退出）'; break
                time.sleep(0.2); continue
            buf += first
        else:
            try: d = sock.recv(65536)
            except socket.timeout: d = None
            except OSError: d = b''
            if d == b'':
                why = '日志口断开（App 退出）'; sock.close(); sock = None; break
            if d: buf += d
        while b'\n' in buf:
            raw, buf = buf.split(b'\n', 1); out.write(raw + b'\n'); line = raw.decode(errors='replace')
            if '看门狗' in line or 'CRASH' in line: why = line.strip()[:80]
            if tS is None and 'UnloadPanel LoadScreenPanel' in line: tS = time.time()
            if tS and tP is None and 'OpenPanel PauseLayout' in line: tP = time.time(); end = tP + pause_s
        if why: break
    stage = '启动' if tS is None else '暂停' if tP is None else '静置'
    ok = not why and tP is not None and time.time() >= tP + pause_s - 0.5
    if not ok and not why: why = '未见读取画面撤下' if tS is None else '未打开暂停菜单' if tP is None else '静置未满'
    if not ok:   # 红：发退出指令之前先留现场（进程还活着、还停在那里） | On failure: capture diagnostics before sending the quit command (the process is still alive and still stopped there)
        if sock is not None:   # 让 App 自己把各客体线程的等待状态、寄存器、图形等待点打进日志（真机上取不了样，只有这一份）；多收 3 秒把它写进 player.txt | Have the App itself print each guest thread's wait state, registers and graphics wait points into the log (sampling is not possible on a real device, this is the only copy); collect 3 more seconds so it gets written into player.txt
            try:
                sock.sendall(b'dump\n'); sock.settimeout(0.3); t1 = time.time() + 3.0
                while time.time() < t1:
                    try: d = sock.recv(65536)
                    except socket.timeout: continue
                    if not d: break
                    out.write(d)
            except OSError: pass
        snapshot_hang(log)
    exited = int(quit_and_confirm(port, sock=sock, out=out)); out.close()
    print(f"result={0 if ok else 1} stage={stage} why='{why}' tS={int(tS - t0) if tS else ''} tP={int(tP - t0) if tP else ''} exited={exited}")
    return 0 if ok else 1


def until(port, log, limit, marker):
    """把日志收到文件，直到出现含 marker 的行或满 limit 秒，然后让 App 退出。标准输出：result=0|1 why=<原因> t=<秒> exited=0|1 | Collect the log to a file until a line containing marker appears or limit seconds elapse, then make the App exit. Stdout: result=0|1 why=<reason> t=<seconds> exited=0|1"""
    t0 = time.time(); out = open(log, 'ab'); sock = None; buf = b''; seen = False; why = ''
    while time.time() < t0 + limit and not seen and not why:
        if sock is None:
            sock, first = connect(port)
            if sock is None:
                time.sleep(0.2); continue
            buf += first
        else:
            try: d = sock.recv(65536)
            except socket.timeout: d = None
            except OSError: d = b''
            if d == b'':
                why = '日志口断开（App 退出）'; sock.close(); sock = None; break
            if d: buf += d
        while b'\n' in buf:
            raw, buf = buf.split(b'\n', 1); out.write(raw + b'\n'); line = raw.decode(errors='replace')
            if '看门狗' in line or 'CRASH' in line: why = line.strip()[:80]
            if marker in line: seen = True
    if not seen and not why: why = f'{int(limit)} 秒内没等到「{marker}」'
    exited = int(quit_and_confirm(port, sock=sock, out=out)); out.close()
    print(f"result={0 if seen and not why else 1} why='{why}' t={int(time.time() - t0)} exited={exited}")
    return 0 if seen and not why else 1


def main(argv):
    cmd, port = argv[1], int(argv[2])
    if cmd == 'alive':
        return 0 if alive(port) else 1
    if cmd == 'quit':
        return 0 if quit_and_confirm(port, float(argv[3]) if len(argv) > 3 else 10.0) else 1
    if cmd == 'player':
        return player(port, argv[3], float(argv[4]), float(argv[5]), float(argv[6]))
    if cmd == 'until':
        return until(port, argv[3], float(argv[4]), argv[5])
    if cmd == 'read':
        return read(port, argv[3], float(argv[4]), float(argv[5]))
    print(__doc__, file=sys.stderr)
    return 2


if __name__ == '__main__':
    sys.exit(main(sys.argv))
