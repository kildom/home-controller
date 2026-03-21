import sys
import os
import re

script_dir = os.path.dirname(os.path.abspath(__file__))

patch = [
    {
        'file': 'Core/Src/main.c',
        'search': re.compile(r'(USART1 Initialization Function[\s\S]*?USART_InitStruct\.BaudRate = )115200'),
        'replace': r'\1PORT0_BAUDRATE',
    },
    {
        'file': 'Core/Src/main.c',
        'search': re.compile(r'(USART2 Initialization Function[\s\S]*?USART_InitStruct\.BaudRate = )115200'),
        'replace': r'\1PORT1_BAUDRATE',
    },
    {
        'file': 'Core/Src/main.c',
        'search': re.compile(r'(PA0[\s\S]*?GPIO_InitStruct\.OutputType = )LL_GPIO_OUTPUT_PUSHPULL'),
        'replace': r'\1(PORT0_OC ? LL_GPIO_OUTPUT_OPENDRAIN : LL_GPIO_OUTPUT_PUSHPULL)',
    },
    {
        'file': 'Core/Src/main.c',
        'search': re.compile(r'(PA2[\s\S]*?GPIO_InitStruct\.OutputType = )LL_GPIO_OUTPUT_PUSHPULL'),
        'replace': r'\1(PORT1_OC ? LL_GPIO_OUTPUT_OPENDRAIN : LL_GPIO_OUTPUT_PUSHPULL)',
    },
]

# --- Apply patches ---
for p in patch:
    file_path = os.path.join(script_dir, p['file'])

    if not os.path.exists(file_path):
        print(f"ERROR: File not found: {p['file']}", file=sys.stderr)
        sys.exit(1)

    with open(file_path, 'r', encoding='utf-8') as f:
        content = f.read()

    if isinstance(p['replace'], list):
        matches = list(p['search'].finditer(content))

        if len(matches) != len(p['replace']):
            print(
                f"ERROR: Match count ({len(matches)}) does not equal replacement array length ({len(p['replace'])}) in file {p['file']}",
                file=sys.stderr,
            )
            sys.exit(1)

        # Build result without re-matching replaced content
        result = ''
        last_index = 0

        for match, replacement in zip(matches, p['replace']):
            result += content[last_index:match.start()]
            result += replacement
            last_index = match.end()

        result += content[last_index:]
        content = result

    else:
        if not p['search'].search(content):
            print(f"ERROR: No matches found in file {p['file']}", file=sys.stderr)
            sys.exit(1)

        content = p['search'].sub(p['replace'], content)

    with open(file_path, 'w', encoding='utf-8') as f:
        f.write(content)

    print(f"Patched: {p['file']}")

print('Done.')
