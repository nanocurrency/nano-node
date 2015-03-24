import sys
import re
import os
import subprocess

if not os.path.exists("bootstrap"):
	os.mkdir("bootstrap")
contents = sys.stdin.read()
subs = re.findall("\{.*?\}", contents, re.DOTALL)
block_number = 0
html_file_name = "./bootstrap/index.html"
html_file = open(html_file_name, "w")
html_file.write("<body>")
for i in subs:
	block_file_name = "./bootstrap/block" + str(block_number) + ".json"
	block_file = open(block_file_name, "w")
	block_file.write(i)
	block_file.close()
	block_file = open(block_file_name, "r")
	qr_file_name = "./bootstrap/block" + str(block_number) + ".png"
	html_file.write("<img src=\"block" + str(block_number) + ".png\"/>")
	subprocess.call(["qrencode", "-o", qr_file_name], stdin=block_file)
	block_number = block_number + 1
html_file.write("</body>")