import sys
import re
import os
import subprocess

if not os.path.exists("bootstrap"):
    os.mkdir("bootstrap")
contents = sys.stdin.read()
subs = re.findall("\{.*?\}", contents, re.DOTALL)
with open("./bootstrap/index.html", "w") as html_file:
    html_file.write("<body>")
    for block_number,sub in enumerate(subs):
        block_number = str(block_number)
        block_file_name = "".join(("./bootstrap/block", block_number, ".json"))
        with open(block_file_name, "w") as block_file:
            block_file.write(sub)
        with open(block_file_name, "r") as block_file:
            qr_file_name = "".join(("./bootstrap/block", block_number, ".png"))
            html_file.write("".join(("<img src=\"block", block_number, ".png\"/>")))
            subprocess.call(["qrencode", "-o", qr_file_name], stdin=block_file)
    html_file.write("</body>")
