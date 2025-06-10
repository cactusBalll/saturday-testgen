import matplotlib.pyplot as plt
import sys
import os
import json

OUTPUT_DIR = 'build/out'
GAUSS_VALEXP = "['s'][0]['c']"
if len(sys.argv) > 1:
    OUTPUT_DIR = sys.argv[1]
if len(sys.argv) > 2:
    GAUSS_VALEXP = sys.argv[2]

data = []

def collect_data(path):
    global data
    if os.path.isdir(path):
        for i in os.listdir(path):
            collect_data(os.path.join(path, i))
        return
    with open(path) as f:
        d = json.load(f)
    data.append(eval('d' + GAUSS_VALEXP))

collect_data(OUTPUT_DIR)
print(data)
# 使用matplotlib的hist()函数绘制直方图
plt.hist(data, bins=30, color='skyblue', alpha=0.7)

# 设置图表的标题和坐标轴标签
plt.title(GAUSS_VALEXP + ' Histogram')
plt.xlabel('Value')
plt.ylabel('Freq')

# 显示图表
plt.show()