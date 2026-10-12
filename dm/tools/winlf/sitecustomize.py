# Windows 上让出包工具链的每个 Python 进程写文本文件时都用 \n 换行（tools/d2rm_pack.py 把本目录放进 PYTHONPATH，
# Python 启动时会自动加载名叫 sitecustomize 的模块，子进程、进程池里的进程也一样）。
# 为什么：Windows 的文本模式缺省把 \n 写成 \r\n。工具链写出的表、清单、着色器源码与说明都要进安装包，多出来的 \r 会让
# 同样的输入在 Windows 上出的包与 macOS / Linux 上的不一样，手机上读表的代码也不认 \r。
# 为什么放在这一处：出库链的几个脚本内容参与翻译链版本，不能逐个去加 newline 参数；在入口统一处理，别的系统上本文件什么都不做。
# On Windows, makes every Python process of the packaging tool chain write text files with LF line endings (tools/d2rm_pack.py puts this directory on PYTHONPATH;
# Python loads a module named sitecustomize automatically at startup, in child processes and pool workers too).
# Why: Windows text mode writes LF as CRLF by default. The tables, lists, shader sources and descriptions written by the tool chain all go into the install package; the extra \r would make
# the package built on Windows differ from the one built on macOS / Linux from the same input, and the table readers on the phone do not accept \r.
# Why here: the contents of the shader chain scripts feed the chain version, so a newline argument cannot be added to them one by one; it is handled once at the entry, and this file does nothing on other systems.
import builtins, io, os

if os.name == 'nt':
    _open = io.open

    def _lf_open(file, mode='r', buffering=-1, encoding=None, errors=None, newline=None, closefd=True, opener=None):
        if newline is None and 'b' not in mode and '+' not in mode and any(c in mode for c in 'wax'): newline = '\n'
        return _open(file, mode, buffering, encoding, errors, newline, closefd, opener)

    builtins.open = io.open = _lf_open
