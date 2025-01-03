import requests
import argparse
from binascii import unhexlify
from base64 import b32decode
from binascii import hexlify, unhexlify


parser = argparse.ArgumentParser(
    description='Generate bootstrap representative weight file.')
parser.add_argument("network", type=str, help="Network name. Eg Live or Beta")
parser.add_argument("--rpc", help="node rpc host:port",
                    default="http://[::1]:7076")
parser.add_argument(
    "--limit", help="percentage of the active supply represented", default=0.99)
parser.add_argument(
    "--cutoff", help="stop using bootstrap reps this many blocks before the current block height", default=250000, type=int)
args = parser.parse_args()

r = requests.post(args.rpc, data='{"action":"representatives"}')
p = r.json()

reps = []
tbl = bytes.maketrans(b'13456789abcdefghijkmnopqrstuwxyz',
                      b'ABCDEFGHIJKLMNOPQRSTUVWXYZ234567')
for acc in p["representatives"]:
    reps.append({
        'account': acc,
        'weight': int(p["representatives"][acc])
    })

r = requests.post(args.rpc, data='{"action":"block_count"}')
p = r.json()
block_height = max(0, int(p["cemented"]) - args.cutoff)

print("cutoff block height is %d" % block_height)

reps.sort(key=lambda x: x["weight"], reverse=True)

supplymax = int(0)
for rep in reps:
    supplymax += rep["weight"]

supplymax /= int('1000000000000000000000000000000')
supplymax = int(supplymax * args.limit)
supplymax *= int('1000000000000000000000000000000')
outputfile = 'bootstrap_weights_' + args.network + '.hpp'

with open(outputfile, 'w') as of:
    of.write(f"#pragma once\n\n#include <string>\n#include <vector>\n\nnamespace nano::weights\n{{\n")
    of.write(f"// Bootstrap weights for {args.network} network\n")
    of.write(f"std::vector<std::pair<std::string, std::string>> preconfigured_weights_{args.network} = {{\n")

    total = int(0)
    count = 0
    for rep in reps:
        if rep["weight"] == 0:
            break
        acc_val = int(hexlify(b32decode(rep["account"].encode(
            'utf-8').replace(b"nano_", b"").translate(tbl) + b"====")), 16)
        
        of.write(f'\t{{ "{rep["account"]}", "{rep["weight"]}" }},\n')
                
        total += rep["weight"]
        count += 1
        print(f'{rep["account"]} {rep["weight"]}')
        if total >= supplymax:
            break
    of.write(f'}};\n')
    of.write(f'uint64_t max_blocks_{args.network} = {block_height};\n')
    of.write(f"}}\n")
    
    print(f"wrote {count} rep weights")
    print(f"max supply {supplymax}")
    print(f"Weight file generated: {outputfile}")

    of.close()
