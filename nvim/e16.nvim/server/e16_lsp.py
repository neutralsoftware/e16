import json
import re
import sys
import traceback

INSTRUCTIONS = {
    "nop": (0x00, 0),
    "mov": (0x01, 2),
    "movb": (0x02, 2),
    "movw": (0x03, 2),
    "clr": (0x04, 1),
    "swap": (0x05, 2),
    "xchg": (0x06, 2),
    "load": (0x10, 2),
    "loadb": (0x11, 2),
    "loadw": (0x12, 2),
    "loadsb": (0x13, 2),
    "store": (0x14, 2),
    "storeb": (0x15, 2),
    "storew": (0x16, 2),
    "addr": (0x17, 2),
    "add": (0x20, 2),
    "addwc": (0x21, 2),
    "sub": (0x22, 2),
    "subwc": (0x23, 2),
    "inc": (0x24, 1),
    "dec": (0x25, 1),
    "neg": (0x26, 1),
    "mul": (0x27, 2),
    "muls": (0x28, 2),
    "div": (0x29, 2),
    "divs": (0x2A, 2),
    "mod": (0x2B, 2),
    "and": (0x30, 2),
    "or": (0x31, 2),
    "xor": (0x32, 2),
    "not": (0x33, 1),
    "test": (0x34, 2),
    "setb": (0x35, 2),
    "clearb": (0x36, 2),
    "toggleb": (0x37, 2),
    "shl": (0x40, 2),
    "shr": (0x41, 2),
    "sar": (0x42, 2),
    "rol": (0x43, 1),
    "ror": (0x44, 1),
    "rcl": (0x45, 1),
    "rcr": (0x46, 1),
    "cmp": (0x50, 2),
    "jmp": (0x60, 1),
    "bra": (0x61, 1),
    "beq": (0x62, 1),
    "bne": (0x63, 1),
    "bcs": (0x64, 1),
    "bcc": (0x65, 1),
    "bmi": (0x66, 1),
    "bpl": (0x67, 1),
    "bvs": (0x68, 1),
    "bvc": (0x69, 1),
    "bgt": (0x6A, 1),
    "bge": (0x6B, 1),
    "blt": (0x6C, 1),
    "ble": (0x6D, 1),
    "bhi": (0x6E, 1),
    "bls": (0x6F, 1),
    "call": (0x70, 1),
    "ret": (0x71, 0),
    "enter": (0x72, 1),
    "leave": (0x73, 0),
    "push": (0x74, 1),
    "pop": (0x75, 1),
    "pushf": (0x76, 0),
    "popf": (0x77, 0),
    "pusha": (0x78, 0),
    "popa": (0x79, 0),
    "int": (0x80, 1),
    "iret": (0x81, 0),
    "ei": (0x82, 0),
    "di": (0x83, 0),
    "wait": (0x84, 0),
    "halt": (0x85, 0),
    "reset": (0x86, 0),
    "trap": (0x87, 0),
    "get": (0x90, 2),
    "set": (0x91, 2),
    "dma": (0xA0, 0),
}

REGISTERS = {
    "r0",
    "r1",
    "r2",
    "r3",
    "r4",
    "r5",
    "r6",
    "r7",
    "r8",
    "r9",
    "r10",
    "r11",
    "r12",
    "r13",
    "r14",
    "r15",
    "pc",
    "sp",
    "fp",
    "fl",
    "dp",
    "ivt",
}

DIRECTIVES = {
    ".const",
    ".constant",
    ".string",
    ".data",
    ".byte",
    ".word",
    ".addr24",
}

TOKEN_TYPES = [
    "namespace",
    "type",
    "class",
    "enum",
    "interface",
    "struct",
    "typeParameter",
    "parameter",
    "variable",
    "property",
    "enumMember",
    "event",
    "function",
    "method",
    "macro",
    "keyword",
    "modifier",
    "comment",
    "string",
    "number",
    "regexp",
    "operator",
    "decorator",
]

TOKEN_INDEX = {name: index for index, name in enumerate(TOKEN_TYPES)}
DOCS = {}
SHUTDOWN = False
IDENT = re.compile(r"[A-Za-z_.][A-Za-z0-9_.]*")


def read_message():
    headers = {}
    while True:
        line = sys.stdin.buffer.readline()
        if not line:
            return None
        line = line.decode("ascii", "replace").strip()
        if not line:
            break
        key, value = line.split(":", 1)
        headers[key.lower()] = value.strip()
    length = int(headers.get("content-length", "0"))
    if length == 0:
        return None
    return json.loads(sys.stdin.buffer.read(length).decode("utf-8"))


def send(payload):
    body = json.dumps(payload, separators=(",", ":")).encode("utf-8")
    sys.stdout.buffer.write(b"Content-Length: " + str(len(body)).encode("ascii") + b"\r\n\r\n")
    sys.stdout.buffer.write(body)
    sys.stdout.buffer.flush()


def respond(message_id, result=None, error=None):
    payload = {"jsonrpc": "2.0", "id": message_id}
    if error is None:
        payload["result"] = result
    else:
        payload["error"] = error
    send(payload)


def notify(method, params):
    send({"jsonrpc": "2.0", "method": method, "params": params})


def split_comment(line):
    quote = ""
    escaped = False
    for index, char in enumerate(line):
        if escaped:
            escaped = False
            continue
        if quote and char == "\\":
            escaped = True
            continue
        if char in ("'", '"'):
            if not quote:
                quote = char
            elif quote == char:
                quote = ""
            continue
        if not quote and char == ";":
            return line[:index], line[index:]
    return line, ""


def split_arguments(text):
    result = []
    current = []
    quote = ""
    escaped = False
    depth = 0
    for char in text:
        if escaped:
            current.append(char)
            escaped = False
            continue
        if quote and char == "\\":
            current.append(char)
            escaped = True
            continue
        if char in ("'", '"'):
            current.append(char)
            if not quote:
                quote = char
            elif quote == char:
                quote = ""
            continue
        if not quote and char == "(":
            depth += 1
        elif not quote and char == ")" and depth > 0:
            depth -= 1
        if not quote and depth == 0 and char == ",":
            value = "".join(current).strip()
            if value:
                result.append(value)
            current = []
            continue
        current.append(char)
    value = "".join(current).strip()
    if value:
        result.append(value)
    return result


def word_at(text, line, character):
    lines = text.splitlines()
    if line < 0 or line >= len(lines):
        return ""
    source = lines[line]
    character = min(character, len(source))
    start = character
    while start > 0 and re.match(r"[A-Za-z0-9_.]", source[start - 1]):
        start -= 1
    end = character
    while end < len(source) and re.match(r"[A-Za-z0-9_.]", source[end]):
        end += 1
    return source[start:end]


def range_for(line, start, length):
    return {
        "start": {"line": line, "character": start},
        "end": {"line": line, "character": start + length},
    }


def location(uri, line, start, length):
    return {"uri": uri, "range": range_for(line, start, length)}


def parse_document(text):
    labels = {}
    constants = {}
    symbols = []
    diagnostics = []
    lines = text.splitlines()
    for line_number, raw in enumerate(lines):
        code, comment = split_comment(raw)
        stripped = code.strip()
        if not stripped:
            continue
        label_match = re.match(r"^\s*([A-Za-z_.][A-Za-z0-9_.]*)\s*:\s*$", code)
        if label_match:
            name = label_match.group(1)
            start = label_match.start(1)
            labels[name] = location("", line_number, start, len(name))
            symbols.append({"name": name, "kind": 12, "range": range_for(line_number, start, len(name)), "selectionRange": range_for(line_number, start, len(name))})
            continue
        const_match = re.match(r"^\s*\.(const|constant)\s+([A-Za-z_.][A-Za-z0-9_.]*)\s*,\s*(.+)$", code)
        if const_match:
            name = const_match.group(2)
            start = const_match.start(2)
            constants[name] = location("", line_number, start, len(name))
            symbols.append({"name": name, "kind": 14, "range": range_for(line_number, start, len(name)), "selectionRange": range_for(line_number, start, len(name))})
            continue
        if stripped.startswith("."):
            directive = stripped.split(None, 1)[0]
            if directive not in DIRECTIVES:
                start = raw.find(directive)
                diagnostics.append(diag(line_number, start, len(directive), "Unknown E16 directive " + directive))
            continue
        match = re.match(r"^\s*([A-Za-z_.][A-Za-z0-9_.]*)(.*)$", code)
        if not match:
            continue
        opcode = match.group(1)
        start = match.start(1)
        if opcode not in INSTRUCTIONS:
            diagnostics.append(diag(line_number, start, len(opcode), "Unknown E16 instruction " + opcode))
            continue
        expected = INSTRUCTIONS[opcode][1]
        operands = split_arguments(match.group(2).strip())
        if len(operands) != expected:
            diagnostics.append(diag(line_number, start, len(opcode), opcode + " expects " + str(expected) + " operand" + ("" if expected == 1 else "s")))
        for operand in operands:
            for reg in re.findall(r"\br[0-9]+\b", operand):
                if reg not in REGISTERS:
                    reg_start = raw.find(reg)
                    diagnostics.append(diag(line_number, reg_start, len(reg), "Invalid E16 register " + reg))
    known = set(labels) | set(constants)
    for line_number, raw in enumerate(lines):
        code, comment = split_comment(raw)
        stripped = code.strip()
        if not stripped or stripped.startswith(".") or stripped.endswith(":"):
            continue
        match = re.match(r"^\s*([A-Za-z_.][A-Za-z0-9_.]*)(.*)$", code)
        if not match or match.group(1) not in INSTRUCTIONS:
            continue
        for token in IDENT.finditer(match.group(2)):
            value = token.group(0)
            if token.start() > 0 and match.group(2)[token.start() - 1].isdigit():
                continue
            if value in REGISTERS or value in INSTRUCTIONS or value in known or value == "dp":
                continue
            if re.match(r"^r[0-9]+$", value):
                continue
            diagnostics.append(diag(line_number, token.start(), len(value), "Unknown E16 symbol " + value, 2))
    return labels, constants, symbols, diagnostics


def diag(line, start, length, message, severity=1):
    return {
        "range": range_for(line, max(start, 0), max(length, 1)),
        "severity": severity,
        "source": "e16-lsp",
        "message": message,
    }


def uri_text(uri):
    return DOCS.get(uri, "")


def publish(uri):
    labels, constants, symbols, diagnostics = parse_document(uri_text(uri))
    notify("textDocument/publishDiagnostics", {"uri": uri, "diagnostics": diagnostics})


def completion_items(uri):
    text = uri_text(uri)
    labels, constants, symbols, diagnostics = parse_document(text)
    items = []
    for name, data in sorted(INSTRUCTIONS.items()):
        items.append({"label": name, "kind": 14, "detail": "opcode " + hex(data[0]) + ", operands " + str(data[1])})
    for name in sorted(REGISTERS):
        items.append({"label": name, "kind": 6, "detail": "E16 register"})
    for name in sorted(DIRECTIVES):
        items.append({"label": name, "kind": 14, "detail": "E16 directive"})
    for name in sorted(labels):
        items.append({"label": name, "kind": 17, "detail": "E16 label"})
    for name in sorted(constants):
        items.append({"label": name, "kind": 21, "detail": "E16 constant"})
    return {"isIncomplete": False, "items": items}


def hover(uri, position):
    text = uri_text(uri)
    word = word_at(text, position["line"], position["character"])
    labels, constants, symbols, diagnostics = parse_document(text)
    if word in INSTRUCTIONS:
        opcode, count = INSTRUCTIONS[word]
        value = word + "\nopcode " + hex(opcode) + "\noperands " + str(count)
    elif word in REGISTERS:
        value = word + "\nE16 register"
    elif word in labels:
        value = word + "\nE16 label"
    elif word in constants:
        value = word + "\nE16 constant"
    elif word in DIRECTIVES:
        value = word + "\nE16 directive"
    elif "." + word in DIRECTIVES:
        value = "." + word + "\nE16 directive"
    else:
        return None
    return {"contents": {"kind": "markdown", "value": "```text\n" + value + "\n```"}}


def definition(uri, position):
    text = uri_text(uri)
    word = word_at(text, position["line"], position["character"])
    labels, constants, symbols, diagnostics = parse_document(text)
    found = labels.get(word) or constants.get(word)
    if not found:
        return None
    found = dict(found)
    found["uri"] = uri
    return found


def document_symbols(uri):
    labels, constants, symbols, diagnostics = parse_document(uri_text(uri))
    return symbols


def add_token(tokens, last, line, start, length, kind):
    if length <= 0:
        return last
    delta_line = line - last[0]
    delta_start = start if delta_line else start - last[1]
    tokens.extend([delta_line, delta_start, length, TOKEN_INDEX[kind], 0])
    return (line, start)


def semantic_tokens(uri):
    text = uri_text(uri)
    labels, constants, symbols, diagnostics = parse_document(text)
    tokens = []
    last = (0, 0)
    for line_number, raw in enumerate(text.splitlines()):
        code, comment = split_comment(raw)
        comment_start = len(code)
        found = []
        const_match = re.match(r"^\s*\.(const|constant)\s+([A-Za-z_.][A-Za-z0-9_.]*)", code)
        label_match = re.match(r"^\s*([A-Za-z_.][A-Za-z0-9_.]*)\s*:\s*$", code)
        directive_match = re.match(r"^\s*(\.[A-Za-z0-9_.]+)", code)
        instr_match = re.match(r"^\s*([A-Za-z_.][A-Za-z0-9_.]*)", code)
        if const_match:
            found.append((const_match.start(1) - 1, len(const_match.group(1)) + 1, "macro"))
            found.append((const_match.start(2), len(const_match.group(2)), "variable"))
        elif label_match:
            found.append((label_match.start(1), len(label_match.group(1)), "function"))
        elif directive_match:
            found.append((directive_match.start(1), len(directive_match.group(1)), "macro"))
        elif instr_match and instr_match.group(1) in INSTRUCTIONS:
            found.append((instr_match.start(1), len(instr_match.group(1)), "keyword"))
        for match in re.finditer(r"\b(r[0-9]+|pc|sp|fp|fl|dp|ivt)\b", code):
            found.append((match.start(), len(match.group(0)), "parameter"))
        for match in re.finditer(r"[-+]?(0x[0-9A-Fa-f]+|0b[01]+|0o[0-7]+|\b[0-9]+\b)", code):
            found.append((match.start(), len(match.group(0)), "number"))
        for match in re.finditer(r'"([^"\\]|\\.)*"|\'([^\'\\]|\\.)*\'', code):
            found.append((match.start(), len(match.group(0)), "string"))
        if comment:
            found.append((comment_start, len(comment), "comment"))
        end = -1
        priority = {"string": 0, "comment": 0, "macro": 1, "keyword": 1, "function": 1, "variable": 1, "parameter": 2, "number": 3}
        for start, length, kind in sorted(found, key=lambda item: (item[0], priority.get(item[2], 9))):
            if start < end:
                continue
            last = add_token(tokens, last, line_number, start, length, kind)
            end = start + length
    return {"data": tokens}


def handle(message):
    global SHUTDOWN
    method = message.get("method")
    params = message.get("params") or {}
    message_id = message.get("id")
    if method == "initialize":
        respond(message_id, {
            "capabilities": {
                "textDocumentSync": 1,
                "completionProvider": {"triggerCharacters": [".", "r", "@", "#"]},
                "hoverProvider": True,
                "definitionProvider": True,
                "documentSymbolProvider": True,
                "semanticTokensProvider": {
                    "legend": {"tokenTypes": TOKEN_TYPES, "tokenModifiers": []},
                    "full": True,
                },
            },
            "serverInfo": {"name": "e16-lsp", "version": "0.1.0"},
        })
    elif method == "shutdown":
        SHUTDOWN = True
        respond(message_id, None)
    elif method == "textDocument/didOpen":
        uri = params["textDocument"]["uri"]
        DOCS[uri] = params["textDocument"].get("text", "")
        publish(uri)
    elif method == "textDocument/didChange":
        uri = params["textDocument"]["uri"]
        changes = params.get("contentChanges", [])
        if changes:
            DOCS[uri] = changes[-1].get("text", DOCS.get(uri, ""))
        publish(uri)
    elif method == "textDocument/didClose":
        uri = params["textDocument"]["uri"]
        DOCS.pop(uri, None)
        notify("textDocument/publishDiagnostics", {"uri": uri, "diagnostics": []})
    elif method == "textDocument/completion":
        respond(message_id, completion_items(params["textDocument"]["uri"]))
    elif method == "textDocument/hover":
        respond(message_id, hover(params["textDocument"]["uri"], params["position"]))
    elif method == "textDocument/definition":
        respond(message_id, definition(params["textDocument"]["uri"], params["position"]))
    elif method == "textDocument/documentSymbol":
        respond(message_id, document_symbols(params["textDocument"]["uri"]))
    elif method == "textDocument/semanticTokens/full":
        respond(message_id, semantic_tokens(params["textDocument"]["uri"]))
    elif message_id is not None:
        respond(message_id, None)


def main():
    while True:
        message = read_message()
        if message is None:
            break
        if message.get("method") == "exit":
            break
        try:
            handle(message)
        except Exception as error:
            if message.get("id") is not None:
                respond(message["id"], error={"code": -32603, "message": str(error), "data": traceback.format_exc()})


if __name__ == "__main__":
    main()
