import subprocess

def execute(cmd):
    popen = subprocess.Popen(cmd, stdout=subprocess.PIPE, universal_newlines=True)
    for stdout_line in iter(popen.stdout.readline, ""):
        yield stdout_line 
    popen.stdout.close()
    return_code = popen.wait()
    if return_code:
        raise subprocess.CalledProcessError(return_code, cmd)

execute("adb logcat -c")
for line in execute("adb logcat -s JNIpart:E"):
  if "Found target at " in line:
    base = line.find("Found target at ")
    base2 = line.find(".00, ")
    base3 = line.find(".00...size ")
    base4 = line.find(".00, ", base3+12)
    base5 = line.find(".00... ratio")
    x = int(line[base+16:base2])
    y = int(line[base2+5:base3])
    w = int(line[base3+11:base4])
    h = int(line[base4+5:base5])
    d_by_w = 5128.205128 / w
    d_by_h = 2595.380223 / h

    d = 6329.113924 / float(w)
    d1 = d+((x - 320.5)**2 * 3.733e-04)+((y - 240.5)** 2 *3.733e-04)
    print "D:",d1, "\t  d_unmodified:",d, "\tx,y:",x,y
