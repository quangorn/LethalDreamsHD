# perf_frida_hook.py — attach frida to the game, load JS hook, print per-2s stats.
# Usage: py -3.12 perf_frida_hook.py <pid> <seconds> <js> <out.jsonl>
import frida, sys, time, json, datetime

pid = int(sys.argv[1]); seconds = float(sys.argv[2]); js = sys.argv[3]; outp = sys.argv[4]
dev = frida.get_local_device()
s = dev.attach(pid)
src = open(js, encoding='utf-8').read()
sc = s.create_script(src)
t0 = datetime.datetime.now()

def on_msg(m, data):
    if m['type'] == 'send':
        p = m['payload']
        if p.get('type') == 'stats':
            line = json.dumps({'t': round((datetime.datetime.now()-t0).total_seconds(), 1), 'stats': p['data']})
            print(line, flush=True)
            with open(outp, 'a', encoding='utf-8') as f:
                f.write(line + '\n')
        else:
            print('MSG', json.dumps(p), flush=True)
    else:
        print('ERR', m, flush=True)

sc.on('message', on_msg)
sc.load()
time.sleep(seconds)
try:
    s.detach()
except Exception:
    pass
print('DONE', flush=True)