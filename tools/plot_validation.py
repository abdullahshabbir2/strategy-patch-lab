"""Optional plot regeneration: python -m pip install matplotlib; python tools/plot_validation.py."""
import csv
from pathlib import Path
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

root=Path(__file__).resolve().parents[1]
def read(name):
    with (root/'evidence'/f'{name}_trace.csv').open() as f:
        return [{k:float(v) for k,v in row.items()} for row in csv.DictReader(f)]
a,b=read('baseline'),read('patched')
t=[r['ms']/1000 for r in b]
plt.rcParams.update({'font.size':10,'axes.spines.top':False,'axes.spines.right':False})
fig,axes=plt.subplots(4,1,figsize=(12,10),sharex=True,layout='constrained')
fig.suptitle('Strategy patch validation — executed host simulation',fontsize=18,fontweight='bold')
axes[0].plot(t,[r['commanded_nm'] for r in a],label='Baseline',color='#8195a5',linewidth=1.6)
axes[0].plot(t,[r['commanded_nm'] for r in b],label='Patched',color='#008d88',linewidth=2)
axes[0].plot(t,[r['limit_nm'] for r in b],label='Patched hard limit',color='#d18428',linestyle='--')
axes[0].set_ylabel('Modeled torque (Nm)'); axes[0].legend(loc='upper left',ncol=3)
axes[1].plot(t,[r['fuel_multiplier'] for r in b],color='#245dab',label='Fuel multiplier')
axes[1].set_ylabel('Fuel mass multiplier'); axes[1].legend(loc='upper left')
axes[2].step(t,[r['map'] for r in b],where='post',label='Active map',color='#245dab')
axes[2].step(t,[r['launch'] for r in b],where='post',label='Launch active',color='#008d88')
axes[2].step(t,[r['shift'] for r in b],where='post',label='Shift active',color='#b45b95')
axes[2].set_ylabel('State'); axes[2].legend(loc='upper left',ncol=3)
axes[3].step(t,[r['limp'] for r in b],where='post',label='Limp active',color='#bc4538')
axes[3].step(t,[r['dtcs'] for r in b],where='post',label='Stored DTC bitmask',color='#5b507a')
axes[3].set_ylabel('Fault state'); axes[3].set_xlabel('Simulated time (s)'); axes[3].legend(loc='upper left',ncol=2)
for ax in axes:
    ax.grid(alpha=.2)
    ax.axvspan(6.6,7.0,color='#da7253',alpha=.12)
    ax.axvspan(7.2,7.6,color='#d5b448',alpha=.12)
    ax.axvspan(7.8,8.4,color='#8b77b1',alpha=.10)
axes[0].text(6.6,370,'Heat',fontsize=9); axes[0].text(7.2,370,'Stale fuel',fontsize=9); axes[0].text(7.9,370,'Tracking',fontsize=9)
fig.savefig(root/'evidence'/'strategy_validation.png',dpi=160)
fig.savefig(root/'evidence'/'strategy_validation.svg')
print('Generated plots from actual executable replay output.')
