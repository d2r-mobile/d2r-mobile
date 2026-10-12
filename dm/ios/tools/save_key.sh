# 本机起 iOS 包的「存档钥匙」：在同一台 Mac、同一个容器里跑局，共用 Documents/save.test；多步用例（第一局写、第二局带 DM_SAVE_TEST=1 接着用）
# 的两步之间别的线一起局就把它换掉，读数会无声对错档。做法：每条线一把钥匙（环境变量 DM_SAVE_KEY，自己取名），钥匙名下的那份测试存档平时收在
# 容器 Data/save.keep.<钥匙>，起局前换进 Documents/save.test，跑完换回去——别的线重置的只是它自己那份。被 smoke_mac.sh、touch_e2e_mac.sh、uireg/run_mac.sh source，
# 都在持 macsmoke 锁之后调（同一时刻只有一个调用方在动容器）。
#   savekey_in <容器 Data 目录> <这一局的环境（空格分隔的 K=V）>   返回非零 = 这一局不许起（已打印红字）
#   savekey_out <容器 Data 目录>                                    收尾时调；没换进过就是空操作
# 抗中断：换进时写标记 Data/save.test.owner（内容 = 钥匙）；局被杀 / 脚本异常退出没来得及换回的，下一个调用方的 savekey_in 先照标记把它归位。
# 不带钥匙：一次性的局（带 DM_SAVE_FRESH，或压根不用测试存档）照旧用公共那份；多步接着用的（DM_SAVE_TEST=1 且没带 DM_SAVE_FRESH）拒绝起局。
# "Save key" for launching the iOS app on this Mac: runs on the same Mac in the same container share Documents/save.test; between the two steps of a multi-step case (first run writes, second run continues with DM_SAVE_TEST=1)
# another developer starting a run replaces it, and the readout is silently checked against the wrong save. What it does: one key per developer (environment variable DM_SAVE_KEY, named as you like); the test save under a key is normally kept in
# the container's Data/save.keep.<key>, swapped into Documents/save.test before a run starts and swapped back afterwards -- another developer's reset only touches its own copy. Sourced by smoke_mac.sh, touch_e2e_mac.sh and uireg/run_mac.sh,
# always called after taking the macsmoke lock (only one caller touches the container at any moment).
#   savekey_in <container Data directory> <this run's environment (space-separated K=V)>   non-zero return = this run must not start (red text already printed)
#   savekey_out <container Data directory>                                    call at wrap-up; a no-op if nothing was swapped in
# Interruption-proof: on swap-in the marker Data/save.test.owner is written (content = key); if a run was killed / the script exited abnormally before swapping back, the next caller's savekey_in first puts it back according to the marker.
# Without a key: one-off runs (with DM_SAVE_FRESH, or not using the test save at all) use the common copy as before; multi-step continuing ones (DM_SAVE_TEST=1 without DM_SAVE_FRESH) are refused.
_sk_pub=_public
_sk_put_back() {   # 把现在 Documents/save.test 这份收回它主人名下，再把公共那份放回原位 | Put the current Documents/save.test back under its owner's name, then put the common copy back in place
  local C=$1 own; own=$(cat "$C/save.test.owner" 2>/dev/null) || return 0; [ -n "$own" ] || { rm -f "$C/save.test.owner"; return 0; }
  rm -rf "$C/Documents/save.test.work" "$C/save.keep.$own"
  [ -d "$C/Documents/save.test" ] && mv "$C/Documents/save.test" "$C/save.keep.$own"
  [ -d "$C/save.keep.$_sk_pub" ] && mv "$C/save.keep.$_sk_pub" "$C/Documents/save.test"
  rm -f "$C/save.test.owner"
}
savekey_in() {
  local C=$1 env=" ${2:-} " key=${DM_SAVE_KEY:-}
  [ -f "$C/save.test.owner" ] && { echo "存档钥匙：上一局（钥匙「$(cat "$C/save.test.owner")」）没换回，先归位" >&2; _sk_put_back "$C"; }
  if [ -z "$key" ]; then
    case "$env" in *" DM_SAVE_TEST=1 "*) case "$env" in *" DM_SAVE_FRESH="*) ;; *)
      echo "【红】这一局带 DM_SAVE_TEST=1 接着用上一局的测试存档，却没带存档钥匙：本机容器是各线共用的，两步之间会被别的局换档。给这几步都带上同一个 DM_SAVE_KEY=<你这条线自己取的名字>；这一局不起" >&2; return 1;; esac;; esac
    return 0
  fi
  case "$key" in *[!A-Za-z0-9_.-]*|"$_sk_pub"|"") echo "【红】DM_SAVE_KEY 只能用字母、数字、_ . -，且不能叫 ${_sk_pub}；这一局不起" >&2; return 1;; esac
  rm -rf "$C/Documents/save.test.work" "$C/save.keep.$_sk_pub"
  [ -d "$C/Documents/save.test" ] && mv "$C/Documents/save.test" "$C/save.keep.$_sk_pub"
  echo "$key" > "$C/save.test.owner"
  if [ -d "$C/save.keep.$key" ]; then mv "$C/save.keep.$key" "$C/Documents/save.test"
  elif [ -d "$C/save.keep.$_sk_pub" ]; then cp -R "$C/save.keep.$_sk_pub" "$C/Documents/save.test"   # 这把钥匙第一次用：从公共那份起步 | First use of this key: start from the common copy
  fi
  return 0
}
savekey_out() { [ -f "$1/save.test.owner" ] && _sk_put_back "$1"; return 0; }
