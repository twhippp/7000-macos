#!/bin/zsh
# n33-desktop.sh - try the GPU desktop on the RX 7600 (kext >= 0.0.672, aux 0.0.8, boot-args from install-metal-mac.sh --desktop-bootargs).
# Order: the console framebuffer (fbpublish), the Metal device (publish), then upstream's arm sequence (tools/pc/arm-gpu-desktop.sh):
# pipeadopt, pipeagdc 1, the headless-no flag, pipearm 1, pipereload, and a WindowServer restart. The restart LOGS YOU OUT: the login
# window comes back drawn by the GPU if it worked. Everything runs detached and logs to ~/navi48-native/desktop.log (and /var/tmp/n33-desktop.log).
# A reboot undoes all of it. Kill switch for the bundle: sudo touch /private/tmp/n48m-off (then restart WindowServer or reboot).
REPO="$(cd "$(dirname "$0")/../.." && pwd)"
N="$REPO/tools/native/n48nub/n48nub"; T="$HOME/navi48-native/navi48test"; LOG="$HOME/navi48-native/desktop.log"
clang -O1 "$REPO/tools/native/n48nub/n48nub.c" -framework IOKit -o "$N" || exit 1
clang -O1 "$REPO/tools/pc/navi48test.c" -framework IOKit -o "$T" 2>/dev/null || { echo "navi48test build failed"; exit 1; }
sudo -v || exit 1
echo "boot-args: $(sysctl -n kern.bootargs)"
case " $(sysctl -n kern.bootargs) " in *" navi48-metal-disp=1 "*) ;; *) echo "navi48-metal-disp=1 is not set: run tools/native/install-metal-mac.sh --desktop-bootargs and reboot first"; exit 1 ;; esac
if ioreg -lw0 | grep -q '"IOClass" = "IONDRVFramebuffer"'; then echo "the generic IONDRVFramebuffer is running: add navi33-fb-boot=1 to boot-args (install-metal-mac.sh --desktop-bootargs) and reboot first"; exit 1; fi
ioreg -lw0 | grep -q '"IOClass" = "Navi48Framebuffer"' || { echo "Navi48Framebuffer is not running: the screen is not ours yet"; exit 1; }
echo "starting; you will be logged out in about 10 s. Log: $LOG (and /var/tmp/n33-desktop.log)"
sudo nohup zsh -c "
exec > /var/tmp/n33-desktop.log 2>&1
sync
say() { print -r -- \"== \$(date +%T) \$*\"; }
say readings; ioreg -l -w0 -c Navi48Bringup | grep -E '\"N33,(Temps|Clocks|ClockGating)\"'
say 'fbpublish (already done at boot)'; '$N' fbpublish; sync; sleep 2
ioreg -lw0 | grep -E '\"IOClass\" = \".*(Framebuffer|NDRV)\"' | head
say 'metal publish'; '$N' publish; sleep 4
kmutil showloaded --list-only 2>/dev/null | grep -E 'navi48'
say pipeadopt; '$T' accel pipeadopt; sync
say 'pipeagdc 1'; '$T' accel pipeagdc 1
touch /private/tmp/n48m-headless-no; chmod 644 /private/tmp/n48m-headless-no
say 'pipearm 1'; '$T' accel pipearm 1; sync
say pipereload; '$T' accel pipereload
say 'restart WindowServer'; killall -9 WindowServer; sleep 20
say 'after restart'; pgrep -lx WindowServer
log show --last 1m --predicate 'eventMessage CONTAINS \"Navi48Metal\"' 2>/dev/null | tail -60
'$T' accel pipestat 4 2>/dev/null | tail -15
ioreg -l -w0 -c Navi48Bringup | grep -E '\"N33,(Temps|Clocks)\"'
say done
cp /var/tmp/n33-desktop.log '$LOG'; chown $(id -u) '$LOG'
" >/dev/null 2>&1 &
