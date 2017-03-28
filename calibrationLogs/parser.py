import sys

if len(sys.argv) != 2:
  print("Error: Invalid arguments!")
  exit

#03-23 11:09:59.894 19128 19155 E JNIpart : Found target at 358.00, 271.00...size 71.00, 36.00... ratio 0.91

data = []
f = open(sys.argv[1], "r")
for line in f.readlines():
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
    data.append((x,y,w,h,d_by_w,d_by_h))
    
f.close()

f = open(sys.argv[1] + ".csv", "w")
f.write("x,y,width,height,WcalcZ,HcalcZ\n")
for pt in data:
  f.write(str(pt)[1:-1] + "\n")
  
