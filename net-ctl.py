#!/usr/bin/env python3
"""Rede da BTV para o menu "Configuração da BTV" (src/setup_menu.cpp).

Uso:
  ./net-ctl.py status              # uma linha por interface + rota padrão
  ./net-ctl.py wait-online SEG     # sai 0 assim que houver rede (boot)
  ./net-ctl.py scan                # redes Wi-Fi visíveis
  ./net-ctl.py wifi-connect SSID [SENHA]
  ./net-ctl.py test URL            # espera a rede e testa o servidor

Saída em linhas separadas por TAB (o app só faz split, sem JSON):
  status:  wired <iface> <carrier 0|1> <ipv4|->
           wifi  <iface> <ssid|->      <ipv4|->
           route <0|1>
  scan:    net <sinal dBm> <salva 0|1> <ssid>
  test:    link ok|fail / http <código>

O box usa netplan + systemd-networkd (Armbian), sem NetworkManager
(decisão de 2026-10-26, ver docs/memory/known-issues.md item -13). As
redes Wi-Fi salvas ficam em SAVED_PATH (ssid -> senha, 0600) e viram
NETPLAN_PATH a cada wifi-connect; o cabo continua com o netplan do
Armbian, que este script nunca toca. Senha vazia = reaproveita a salva.

A senha chega por argv (o app não tem stdin pro filho): só root e
`nobody` (triggerhappy) rodam neste box.
"""

import json
import os
import subprocess
import sys
import time

SAVED_PATH = "/etc/tvbox/wifi-networks.json"
NETPLAN_PATH = "/etc/netplan/90-tvbox-wifi.yaml"
SYS_NET = "/sys/class/net"


def run(argv, timeout=20):
    try:
        return subprocess.run(argv, capture_output=True, text=True, timeout=timeout)
    except (OSError, subprocess.TimeoutExpired):
        return None


def interfaces(prefix):
    try:
        return sorted(n for n in os.listdir(SYS_NET) if n.startswith(prefix))
    except OSError:
        return []


def read_sys(iface, name):
    try:
        with open(os.path.join(SYS_NET, iface, name)) as f:
            return f.read().strip()
    except OSError:
        return ""


def ipv4(iface):
    r = run(["ip", "-4", "-o", "addr", "show", "dev", iface])
    if r is None or r.returncode != 0:
        return ""
    for line in r.stdout.splitlines():
        parts = line.split()
        if "inet" in parts:
            return parts[parts.index("inet") + 1].split("/")[0]
    return ""


def connected_ssid(iface):
    r = run(["iw", "dev", iface, "link"])
    if r is None:
        return ""
    for line in r.stdout.splitlines():
        line = line.strip()
        if line.startswith("SSID:"):
            return line[5:].strip()
    return ""


def has_default_route():
    r = run(["ip", "-4", "route", "show", "default"])
    return r is not None and r.returncode == 0 and r.stdout.strip() != ""


def online():
    """Rede = rota padrão + IPv4 em alguma interface (cabo ou Wi-Fi).
    Não testa a internet: o manual separa "sem rede" de "servidor fora"."""
    if not has_default_route():
        return False
    return any(ipv4(i) for i in interfaces("e") + interfaces("wl"))


def load_saved():
    try:
        with open(SAVED_PATH) as f:
            data = json.load(f)
        return data if isinstance(data, dict) else {}
    except (OSError, ValueError):
        return {}


def write_private(path, text):
    """Grava com 0600 + fsync (o box perde escritas recentes ao tirar da
    tomada, ver known-issues item -12)."""
    os.makedirs(os.path.dirname(path), exist_ok=True)
    tmp = path + ".tmp"
    fd = os.open(tmp, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
    with os.fdopen(fd, "w") as f:
        f.write(text)
        f.flush()
        os.fsync(f.fileno())
    os.replace(tmp, path)
    dfd = os.open(os.path.dirname(path), os.O_RDONLY)
    try:
        os.fsync(dfd)
    finally:
        os.close(dfd)


def cmd_status():
    for iface in interfaces("e"):
        carrier = "1" if read_sys(iface, "carrier") == "1" else "0"
        print(f"wired\t{iface}\t{carrier}\t{ipv4(iface) or '-'}")
    for iface in interfaces("wl"):
        print(f"wifi\t{iface}\t{connected_ssid(iface) or '-'}\t{ipv4(iface) or '-'}")
    print(f"route\t{1 if has_default_route() else 0}")
    return 0


def cmd_wait_online(seconds):
    deadline = time.monotonic() + seconds
    while True:
        if online():
            return 0
        if time.monotonic() >= deadline:
            return 1
        time.sleep(1)


def cmd_scan():
    wl = interfaces("wl")
    if not wl:
        print("Nenhuma interface Wi-Fi (conecte o dongle USB)", file=sys.stderr)
        return 3
    iface = wl[0]
    run(["ip", "link", "set", iface, "up"])
    r = run(["iw", "dev", iface, "scan"], timeout=30)
    if r is None or r.returncode != 0:
        print("Falha ao procurar redes", file=sys.stderr)
        return 1
    saved = load_saved()
    best = {}
    signal = -100.0
    for line in r.stdout.splitlines():
        line = line.strip()
        if line.startswith("BSS "):
            signal = -100.0
        elif line.startswith("signal:"):
            try:
                signal = float(line.split()[1])
            except (IndexError, ValueError):
                pass
        elif line.startswith("SSID:"):
            ssid = line[5:].strip()
            if ssid and signal > best.get(ssid, -1000.0):
                best[ssid] = signal
    for ssid, sig in sorted(best.items(), key=lambda kv: -kv[1]):
        print(f"net\t{int(sig)}\t{1 if ssid in saved else 0}\t{ssid}")
    return 0


def cmd_wifi_connect(ssid, password):
    wl = interfaces("wl")
    if not wl:
        print("Nenhuma interface Wi-Fi (conecte o dongle USB)", file=sys.stderr)
        return 3
    saved = load_saved()
    if not password:
        if ssid not in saved:
            print("Rede nova: digite a senha no teclado", file=sys.stderr)
            return 2
        password = saved[ssid]
    saved[ssid] = password
    write_private(SAVED_PATH, json.dumps(saved, ensure_ascii=False, indent=2) + "\n")

    # JSON é YAML válido: evita escapar SSID/senha na mão. Todas as redes
    # salvas entram, pra o wpa_supplicant achar qualquer uma no próximo boot.
    aps = {s: ({"password": p} if p else {}) for s, p in saved.items()}
    doc = {"network": {"version": 2, "renderer": "networkd",
                       "wifis": {wl[0]: {"dhcp4": True, "access-points": aps}}}}
    write_private(NETPLAN_PATH, json.dumps(doc, ensure_ascii=False, indent=2) + "\n")
    r = run(["netplan", "apply"], timeout=60)
    if r is None or r.returncode != 0:
        print("netplan apply falhou", file=sys.stderr)
        return 1
    return 0


def cmd_test(url):
    link = cmd_wait_online(20) == 0
    print(f"link\t{'ok' if link else 'fail'}")
    code = "000"
    if link:
        r = run(["curl", "-s", "-o", "/dev/null", "-I", "-w", "%{http_code}",
                 "--max-time", "10", url], timeout=15)
        if r is not None and r.stdout.strip():
            code = r.stdout.strip()
    print(f"http\t{code}")
    return 0 if link else 1


def main(argv):
    if len(argv) >= 2 and argv[1] == "status":
        return cmd_status()
    if len(argv) == 3 and argv[1] == "wait-online":
        return cmd_wait_online(int(argv[2]))
    if len(argv) == 2 and argv[1] == "scan":
        return cmd_scan()
    if len(argv) in (3, 4) and argv[1] == "wifi-connect":
        return cmd_wifi_connect(argv[2], argv[3] if len(argv) == 4 else "")
    if len(argv) == 3 and argv[1] == "test":
        return cmd_test(argv[2])
    print(__doc__.split("\n\n")[1], file=sys.stderr)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
