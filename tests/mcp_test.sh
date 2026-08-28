#!/bin/sh
# mcp_test.sh — MCP server smoke test: boots the ROM, drives a shell through
# the terminal over JSON-RPC, checks framing, text waits and a PNG screenshot.
# The $(echo OK) trick keeps the expected output off the typed command line.
set -e

BIN=${1:-./ezalb}
ROM=${2:-vt420}
OUT=$(mktemp)
GIF="$OUT.gif"
trap 'rm -f "$OUT" "$GIF"' EXIT

{
  echo '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-06-18","capabilities":{},"clientInfo":{"name":"mcp-test","version":"0"}}}'
  echo '{"jsonrpc":"2.0","method":"notifications/initialized"}'
  echo '{"jsonrpc":"2.0","id":2,"method":"tools/list"}'
  echo '{"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"wait","arguments":{"for_text":"$","timeout_ms":60000}}}'
  echo '{"jsonrpc":"2.0","id":4,"method":"tools/call","params":{"name":"type","arguments":{"text":"echo MCP-SMOKE-$(echo OK)\r"}}}'
  echo '{"jsonrpc":"2.0","id":5,"method":"tools/call","params":{"name":"wait","arguments":{"for_text":"MCP-SMOKE-OK","timeout_ms":60000}}}'
  echo '{"jsonrpc":"2.0","id":6,"method":"tools/call","params":{"name":"screenshot","arguments":{}}}'
  echo '{"jsonrpc":"2.0","id":7,"method":"tools/call","params":{"name":"video","arguments":{"path":"'"$GIF"'","duration_ms":500,"fps":10}}}'
  echo '{"jsonrpc":"2.0","id":8,"method":"tools/call","params":{"name":"status","arguments":{}}}'
  echo '{"jsonrpc":"2.0","id":9,"method":"tools/call","params":{"name":"nosuch","arguments":{}}}'
} | "$BIN" --mcp --rom "$ROM" --skip-diagnostics --comm1 'exec /bin/sh' > "$OUT" 2>/dev/null

check() {
    if ! grep -q "$1" "$OUT"; then
        echo "FAIL: missing $2"
        exit 1
    fi
}

check '"protocolVersion"'                  "initialize response"
check '"name":"screenshot"'                "tools/list entry"
check '"text":"matched'                    "wait for_text match"
check '"type":"image","data":"iVBORw0KGgo' "PNG screenshot"
check '"text":"wrote'                      "video tool wrote a file"
if [ "$(head -c 6 "$GIF")" != "GIF89a" ]; then
    echo "FAIL: video did not produce a GIF"
    exit 1
fi
check '\\"columns\\":80'                   "status columns"
check '"code":-32602'                      "unknown-tool error"
if grep -q 'MCP-SMOKE-OK' "$OUT" && sed -n '5p' "$OUT" | grep -q '"text":"matched'; then
    :
else
    echo "FAIL: shell echo did not reach the screen"
    exit 1
fi
echo PASS
