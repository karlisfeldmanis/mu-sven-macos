path = '/Users/karlisfeldmanis/Desktop/mu_remaster/server/src/Database.cpp'
with open(path, 'r') as f:
    content = f.read()

# Filter out comments and raw strings to avoid false positives
import re

# Remove raw strings R"( ... )"
content = re.sub(r'R"\((.*?)\)"', '', content, flags=re.DOTALL)
# Remove single line comments
content = re.sub(r'//.*', '', content)
# Remove multi line comments
content = re.sub(r'/\*.*?\*/', '', content, flags=re.DOTALL)

balance = 0
for i, char in enumerate(content):
    if char == '{':
        balance += 1
    elif char == '}':
        balance -= 1
        if balance < 0:
            print(f"Extra closing brace at position {i}")
            # Find line number
            line_num = content.count('\n', 0, i) + 1
            print(f"Line number: ~{line_num}")
            break

print(f"Final balance: {balance}")
if balance > 0:
    print("Missing closing brace(s)")
elif balance == 0:
    print("Braces are balanced (ignoring comments/strings)")
