/* mcp.c — MCP stdio server: newline-delimited JSON-RPC 2.0 exposing the
 * control core (ctl.h) as tools with inline PNG screenshots. */
#include "host/mcp.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "host/ctl.h"
#include "host/json.h"

#define MCP_POOL_CAP      512
#define MCP_DEFAULT_SPEED 10.0
#define MCP_PROTOCOL      "2025-06-18"

static vt_ctl g_ctl;
static char   g_screen[CTL_SCREEN_CAP];

static const char TOOLS_JSON[] = "{\"tools\":["
    "{\"name\":\"screenshot\","
    "\"description\":\"Capture the VT420 screen as a PNG image (800x416). By "
    "default waits for the display to settle first. Prefer read_screen for "
    "checking text; use this to verify rendering (double width/height, soft "
    "fonts, reverse video, Set-Up screens).\","
    "\"inputSchema\":{\"type\":\"object\",\"properties\":{"
    "\"settle\":{\"type\":\"boolean\",\"description\":\"wait for a stable screen first (default true)\"},"
    "\"max_wait_ms\":{\"type\":\"number\",\"description\":\"settle cap in emulated ms (default 2000)\"}}}},"

    "{\"name\":\"read_screen\","
    "\"description\":\"Read the VT420 screen as text, rows separated by "
    "newlines (unmapped/soft-font glyphs shown as '.'). Much cheaper than "
    "screenshot. attrs=true appends @row lines describing cell attributes "
    "(bold/rev/ul/blink) and row modes (dw/dht/dhb = double width/height).\","
    "\"inputSchema\":{\"type\":\"object\",\"properties\":{"
    "\"attrs\":{\"type\":\"boolean\",\"description\":\"append attribute annotations (default false)\"},"
    "\"settle\":{\"type\":\"boolean\",\"description\":\"wait for a stable screen first (default true)\"}}}},"

    "{\"name\":\"type\","
    "\"description\":\"Type text on the LK201 keyboard, paced so the firmware "
    "keeps up. \\r or \\n = Return, \\t = Tab, \\u001b = ESC, 0x01-0x1a = "
    "Ctrl+letter, 0x7f = Delete. ASCII only. End shell commands with \\r.\","
    "\"inputSchema\":{\"type\":\"object\",\"properties\":{"
    "\"text\":{\"type\":\"string\"}},\"required\":[\"text\"]}},"

    "{\"name\":\"key\","
    "\"description\":\"Press a named key: f1..f20, setup (=F3, toggles Set-Up), "
    "enter, tab, delete, esc, up/down/left/right, find/home, select/end, "
    "prev/pgup, next/pgdn, insert, remove, help, do, compose, lock, kp0..kp9, "
    "kpenter, kpdot, kpcomma, kpminus, pf1..pf4, or a single ASCII character. "
    "f4 switches session on dual-session setups. Modifiers: ctrl, shift.\","
    "\"inputSchema\":{\"type\":\"object\",\"properties\":{"
    "\"name\":{\"type\":\"string\"},"
    "\"ctrl\":{\"type\":\"boolean\"},\"shift\":{\"type\":\"boolean\"},"
    "\"count\":{\"type\":\"number\",\"description\":\"repeat count (default 1)\"}},"
    "\"required\":[\"name\"]}},"

    "{\"name\":\"wait\","
    "\"description\":\"Advance emulated time. With for_text: run until the "
    "screen contains the substring or timeout_ms emulated ms pass, then return "
    "the screen text (like expect). Without for_text: run ms emulated "
    "milliseconds.\","
    "\"inputSchema\":{\"type\":\"object\",\"properties\":{"
    "\"for_text\":{\"type\":\"string\",\"description\":\"substring to wait for\"},"
    "\"timeout_ms\":{\"type\":\"number\",\"description\":\"for_text timeout, emulated ms (default 10000)\"},"
    "\"ms\":{\"type\":\"number\",\"description\":\"plain wait duration, emulated ms\"}}}},"

    "{\"name\":\"record\","
    "\"description\":\"Capture a series of PNG frames at a fixed emulated-time "
    "interval — for blink, smooth scrolling, or animations. Returns all frames "
    "as images. Use video to save a recording to a file instead.\","
    "\"inputSchema\":{\"type\":\"object\",\"properties\":{"
    "\"frames\":{\"type\":\"number\",\"description\":\"frame count, 1-8 (default 4)\"},"
    "\"interval_ms\":{\"type\":\"number\",\"description\":\"emulated ms between frames (default 500)\"}}}},"

    "{\"name\":\"video\","
    "\"description\":\"Record the screen to an animated GIF file: a real "
    "recording to keep or share (README demos, bug reports), unlike record "
    "which returns a few frames inline for you to look at. duration_ms is "
    "emulated time and plays back at that speed — the machine free-runs at "
    "10x real time, so pace {speed:1} first when recording a program's "
    "output, or it will trail the recording.\","
    "\"inputSchema\":{\"type\":\"object\",\"properties\":{"
    "\"path\":{\"type\":\"string\",\"description\":\"output file, e.g. screenshots/demo.gif\"},"
    "\"duration_ms\":{\"type\":\"number\",\"description\":\"emulated ms to record (default 3000)\"},"
    "\"fps\":{\"type\":\"number\",\"description\":\"frames per second, 1-50 (default 10)\"}},"
    "\"required\":[\"path\"]}},"

    "{\"name\":\"reset\","
    "\"description\":\"Power-cycle the terminal: fresh boot with the same ROM, "
    "NVR and session configs.\","
    "\"inputSchema\":{\"type\":\"object\",\"properties\":{}}},"

    "{\"name\":\"session\","
    "\"description\":\"Reconnect a comm port to a new session at runtime. "
    "config: 'exec CMD [--no-pty] [--rows N] [--cols N]' | 'pipe PATH' | "
    "'loopback'. Typical loop: rebuild your app, then session {port:1, "
    "config:\\\"exec ./myapp\\\"}. IMPORTANT: wait {for_text} for the "
    "program's prompt/output before typing — input typed before a pty "
    "program finishes starting is flushed, like on real serial hardware.\","
    "\"inputSchema\":{\"type\":\"object\",\"properties\":{"
    "\"port\":{\"type\":\"number\",\"description\":\"1 or 2 (default 1)\"},"
    "\"config\":{\"type\":\"string\"}},\"required\":[\"config\"]}},"

    "{\"name\":\"pace\","
    "\"description\":\"Emulation pacing. mode=free: run continuously at speed x "
    "real time (default 10, 0 = unlimited). mode=paused: freeze time except "
    "during tool calls — deterministic screenshots.\","
    "\"inputSchema\":{\"type\":\"object\",\"properties\":{"
    "\"mode\":{\"type\":\"string\",\"enum\":[\"free\",\"paused\"]},"
    "\"speed\":{\"type\":\"number\"}}}},"

    "{\"name\":\"status\","
    "\"description\":\"Machine status: instruction count, emulated uptime, "
    "80/132 columns, active screen, row count, session configs, DTR, pacing, "
    "pending keyboard bytes.\","
    "\"inputSchema\":{\"type\":\"object\",\"properties\":{}}}"
    "]}";

/* response plumbing */

static void jb_id(json_buf *b, const json_node *id)
{
    if (id && id->kind == J_NUM) {
        if (id->num == (double)(long long)id->num)
            jb_fmt(b, "%lld", (long long)id->num);
        else
            jb_fmt(b, "%.17g", id->num);
    } else if (id && id->kind == J_STR) {
        jb_str(b, id->str);
    } else {
        jb_raw(b, "null");
    }
}

static void send_line(json_buf *b)
{
    fwrite(b->p, 1, b->len, stdout);
    fputc('\n', stdout);
    fflush(stdout);
}

static void reply_result(const json_node *id, const char *result_json)
{
    json_buf b;

    json_buf_init(&b);
    jb_raw(&b, "{\"jsonrpc\":\"2.0\",\"id\":");
    jb_id(&b, id);
    jb_raw(&b, ",\"result\":");
    jb_raw(&b, result_json);
    jb_raw(&b, "}");
    send_line(&b);
    json_buf_free(&b);
}

static void reply_error(const json_node *id, int code, const char *msg)
{
    json_buf b;

    json_buf_init(&b);
    jb_raw(&b, "{\"jsonrpc\":\"2.0\",\"id\":");
    jb_id(&b, id);
    jb_fmt(&b, ",\"error\":{\"code\":%d,\"message\":", code);
    jb_str(&b, msg);
    jb_raw(&b, "}}");
    send_line(&b);
    json_buf_free(&b);
}

/* {"content":[{"type":"text","text":<text>}],"isError":<err>} */
static void reply_text(const json_node *id, const char *text, bool is_error)
{
    json_buf b;

    json_buf_init(&b);
    jb_raw(&b, "{\"jsonrpc\":\"2.0\",\"id\":");
    jb_id(&b, id);
    jb_raw(&b, ",\"result\":{\"content\":[{\"type\":\"text\",\"text\":");
    jb_str(&b, text);
    jb_fmt(&b, "}],\"isError\":%s}}", is_error ? "true" : "false");
    send_line(&b);
    json_buf_free(&b);
}

static void jb_image(json_buf *b, const uint8_t *png, size_t len)
{
    jb_raw(b, "{\"type\":\"image\",\"data\":\"");
    jb_base64(b, png, len);
    jb_raw(b, "\",\"mimeType\":\"image/png\"}");
}

static void caption(char *buf, size_t cap)
{
    ctl_status_info st;

    ctl_get_status(&g_ctl, &st);
    snprintf(buf, cap, "%s cols, %u rows, screen %d, %.1fs emulated",
             st.cols_132 ? "132" : "80", st.rows, st.screen_2 ? 2 : 1,
             st.emulated_s);
}

/* tool handlers */

static void tool_screenshot(const json_node *id, const json_node *args)
{
    uint8_t *png;
    size_t len;
    char cap[128];

    if (ctl_capture(&g_ctl, json_bool_of(args, "settle", true),
                    (uint32_t)json_num_of(args, "max_wait_ms", 2000),
                    &png, &len) != 0) {
        reply_text(id, "screenshot failed: PNG encode error", true);
        return;
    }
    caption(cap, sizeof cap);

    json_buf b;
    json_buf_init(&b);
    jb_raw(&b, "{\"jsonrpc\":\"2.0\",\"id\":");
    jb_id(&b, id);
    jb_raw(&b, ",\"result\":{\"content\":[");
    jb_image(&b, png, len);
    jb_raw(&b, ",{\"type\":\"text\",\"text\":");
    jb_str(&b, cap);
    jb_raw(&b, "}],\"isError\":false}}");
    send_line(&b);
    json_buf_free(&b);
    free(png);
}

static void tool_read_screen(const json_node *id, const json_node *args)
{
    if (json_bool_of(args, "settle", true))
        ctl_settle(&g_ctl, 2000);
    ctl_read_screen(&g_ctl, g_screen, sizeof g_screen,
                    json_bool_of(args, "attrs", false));
    reply_text(id, g_screen, false);
}

static void tool_type(const json_node *id, const json_node *args)
{
    const char *text = json_str_of(args, "text", NULL);
    char err[256];

    if (!text) {
        reply_error(id, -32602, "type: missing \"text\"");
        return;
    }
    if (ctl_type(&g_ctl, text, err, sizeof err) != 0) {
        reply_text(id, err, true);
        return;
    }
    reply_text(id, "ok", false);
}

static void tool_key(const json_node *id, const json_node *args)
{
    const char *name = json_str_of(args, "name", NULL);
    char err[256];

    if (!name) {
        reply_error(id, -32602, "key: missing \"name\"");
        return;
    }
    if (ctl_key(&g_ctl, name, json_bool_of(args, "ctrl", false),
                json_bool_of(args, "shift", false),
                (int)json_num_of(args, "count", 1), err, sizeof err) != 0) {
        reply_text(id, err, true);
        return;
    }
    reply_text(id, "ok", false);
}

static void tool_wait(const json_node *id, const json_node *args)
{
    const char *needle = json_str_of(args, "for_text", NULL);

    if (!needle) {
        uint32_t ms = (uint32_t)json_num_of(args, "ms", 0);
        if (ms == 0) {
            reply_error(id, -32602, "wait: need \"for_text\" or \"ms\"");
            return;
        }
        ctl_wait_ms(&g_ctl, ms);
        reply_text(id, "ok", false);
        return;
    }

    uint32_t timeout = (uint32_t)json_num_of(args, "timeout_ms", 10000);
    bool found = ctl_wait_text(&g_ctl, needle, timeout);
    ctl_read_screen(&g_ctl, g_screen, sizeof g_screen, false);

    json_buf b;
    json_buf_init(&b);
    if (found)
        jb_raw(&b, "matched\n---\n");
    else
        jb_fmt(&b, "TIMEOUT: \"%s\" not on screen after %u emulated ms\n---\n",
               needle, timeout);
    jb_raw(&b, g_screen);
    reply_text(id, b.p, !found);
    json_buf_free(&b);
}

static void tool_record(const json_node *id, const json_node *args)
{
    int frames = (int)json_num_of(args, "frames", 4);
    uint32_t interval = (uint32_t)json_num_of(args, "interval_ms", 500);
    char cap[128];

    if (frames < 1)
        frames = 1;
    if (frames > 8)
        frames = 8;
    if (interval < 1)
        interval = 1;

    json_buf b;
    json_buf_init(&b);
    jb_raw(&b, "{\"jsonrpc\":\"2.0\",\"id\":");
    jb_id(&b, id);
    jb_raw(&b, ",\"result\":{\"content\":[");
    for (int i = 0; i < frames; i++) {
        uint8_t *png;
        size_t len;
        if (i > 0) {
            ctl_wait_ms(&g_ctl, interval);
            jb_raw(&b, ",");
        }
        if (ctl_capture(&g_ctl, false, 0, &png, &len) != 0) {
            json_buf_free(&b);
            reply_text(id, "record failed: PNG encode error", true);
            return;
        }
        jb_image(&b, png, len);
        free(png);
    }
    snprintf(cap, sizeof cap, "%d frames, %u emulated ms apart", frames, interval);
    jb_raw(&b, ",{\"type\":\"text\",\"text\":");
    jb_str(&b, cap);
    jb_raw(&b, "}],\"isError\":false}}");
    send_line(&b);
    json_buf_free(&b);
}

static void tool_video(const json_node *id, const json_node *args)
{
    const char *path = json_str_of(args, "path", NULL);
    uint32_t frames = 0;
    long bytes = 0;
    char err[256], msg[512];

    if (!path) {
        reply_error(id, -32602, "video: missing \"path\"");
        return;
    }
    uint32_t duration = (uint32_t)json_num_of(args, "duration_ms", 3000);
    if (ctl_record(&g_ctl, path, duration, (uint32_t)json_num_of(args, "fps", 10),
                   &frames, &bytes, err, sizeof err) != 0) {
        reply_text(id, err, true);
        return;
    }
    snprintf(msg, sizeof msg, "wrote %s: %u frames, %dx%d, %.1f KB, %.1fs of "
             "emulated time", path, frames, FB_WIDTH, FB_HEIGHT,
             (double)bytes / 1024.0, (double)duration / 1000.0);
    reply_text(id, msg, false);
}

static void tool_reset(const json_node *id)
{
    char err[256];

    if (ctl_reset(&g_ctl, err, sizeof err) != 0) {
        reply_text(id, err, true);
        return;
    }
    reply_text(id, "power-cycled", false);
}

static void tool_session(const json_node *id, const json_node *args)
{
    const char *cfg = json_str_of(args, "config", NULL);
    char err[256];

    if (!cfg) {
        reply_error(id, -32602, "session: missing \"config\"");
        return;
    }
    if (ctl_session(&g_ctl, (int)json_num_of(args, "port", 1), cfg,
                    err, sizeof err) != 0) {
        reply_text(id, err, true);
        return;
    }
    reply_text(id, "session reconnected", false);
}

static void tool_pace(const json_node *id, const json_node *args)
{
    ctl_status_info st;

    ctl_get_status(&g_ctl, &st);
    const char *mode = json_str_of(args, "mode", st.paused ? "paused" : "free");
    double speed = json_num_of(args, "speed", st.speed);
    bool paused = strcmp(mode, "paused") == 0;

    ctl_pace(&g_ctl, paused, speed);
    char msg[96];
    if (paused)
        snprintf(msg, sizeof msg, "paused (time advances only during tool calls)");
    else if (speed <= 0)
        snprintf(msg, sizeof msg, "free-running, unlimited speed");
    else
        snprintf(msg, sizeof msg, "free-running at %.1fx real time", speed);
    reply_text(id, msg, false);
}

static void tool_status(const json_node *id)
{
    ctl_status_info st;
    json_buf b;

    ctl_get_status(&g_ctl, &st);
    json_buf_init(&b);
    jb_fmt(&b, "{\"instructions\":%zu,\"emulated_s\":%.2f,"
               "\"columns\":%d,\"rows\":%u,\"screen\":%d,"
               "\"paused\":%s,\"speed\":%.1f,\"kbd_pending\":%u,"
               "\"dtr1\":%s,\"dtr2\":%s,",
           st.instruction_count, st.emulated_s, st.cols_132 ? 132 : 80,
           st.rows, st.screen_2 ? 2 : 1, st.paused ? "true" : "false",
           st.speed, st.kbd_pending,
           st.dtr_a ? "true" : "false", st.dtr_b ? "true" : "false");
    jb_raw(&b, "\"comm1\":");
    jb_str(&b, st.comm1);
    jb_raw(&b, ",\"comm2\":");
    jb_str(&b, st.comm2);
    jb_raw(&b, "}");
    reply_text(id, b.p, false);
    json_buf_free(&b);
}

/* dispatch */

static void handle_initialize(const json_node *id, const json_node *params)
{
    const char *ver = json_str_of(params, "protocolVersion", MCP_PROTOCOL);
    ctl_status_info st;
    json_buf b;

    ctl_get_status(&g_ctl, &st);
    json_buf_init(&b);
    jb_raw(&b, "{\"protocolVersion\":");
    jb_str(&b, ver);
    jb_raw(&b, ",\"capabilities\":{\"tools\":{}},"
               "\"serverInfo\":{\"name\":\"vt420\",\"version\":\"0.1.0\"},"
               "\"instructions\":");
    json_buf i;
    json_buf_init(&i);
    jb_fmt(&i, "A real VT420 terminal (hardware emulation running DEC "
               "firmware) under your control. comm1: %s, comm2: %s. Typical "
               "flow: wait {for_text} for output, type {text ending in \\r}, "
               "read_screen for text, screenshot for pixels, video for a GIF "
               "recording. key {name: \"setup\"} toggles Set-Up. Time runs at "
               "10x real time between calls; waits are in emulated ms. After "
               "reset or session, always wait {for_text} for the program's "
               "prompt before typing.", st.comm1, st.comm2);
    jb_str(&b, i.p);
    json_buf_free(&i);
    jb_raw(&b, "}");
    reply_result(id, b.p);
    json_buf_free(&b);
}

static void handle_line(char *line)
{
    static json_node pool[MCP_POOL_CAP];
    json_node *root = json_parse(line, pool, MCP_POOL_CAP);

    if (!root) {
        reply_error(NULL, -32700, "parse error");
        return;
    }
    const char *method = json_str_of(root, "method", NULL);
    const json_node *id = json_get(root, "id");
    const json_node *params = json_get(root, "params");

    if (!method) {
        if (id)
            reply_error(id, -32600, "missing method");
        return;
    }
    if (strncmp(method, "notifications/", 14) == 0)
        return;
    if (strcmp(method, "initialize") == 0) {
        handle_initialize(id, params);
        return;
    }
    if (strcmp(method, "ping") == 0) {
        reply_result(id, "{}");
        return;
    }
    if (strcmp(method, "tools/list") == 0) {
        reply_result(id, TOOLS_JSON);
        return;
    }
    if (strcmp(method, "tools/call") == 0) {
        const char *name = json_str_of(params, "name", "");
        const json_node *args = json_get(params, "arguments");

        if (strcmp(name, "screenshot") == 0)       tool_screenshot(id, args);
        else if (strcmp(name, "read_screen") == 0) tool_read_screen(id, args);
        else if (strcmp(name, "type") == 0)        tool_type(id, args);
        else if (strcmp(name, "key") == 0)         tool_key(id, args);
        else if (strcmp(name, "wait") == 0)        tool_wait(id, args);
        else if (strcmp(name, "record") == 0)      tool_record(id, args);
        else if (strcmp(name, "video") == 0)       tool_video(id, args);
        else if (strcmp(name, "reset") == 0)       tool_reset(id);
        else if (strcmp(name, "session") == 0)     tool_session(id, args);
        else if (strcmp(name, "pace") == 0)        tool_pace(id, args);
        else if (strcmp(name, "status") == 0)      tool_status(id);
        else reply_error(id, -32602, "unknown tool");
        return;
    }
    if (id)
        reply_error(id, -32601, "method not found");
}

int mcp_run(const uint8_t *rom, uint32_t rom_len, const char *nvr_path,
            const char *comm1, const char *comm2, bool skip_diagnostics)
{
    char err[256];

    if (ctl_start(&g_ctl, rom, rom_len, nvr_path, comm1, comm2,
                  skip_diagnostics, MCP_DEFAULT_SPEED, err, sizeof err) != 0) {
        fprintf(stderr, "mcp: %s\n", err);
        return 1;
    }

    char *line = NULL;
    size_t cap = 0;
    while (getline(&line, &cap, stdin) > 0)
        handle_line(line);

    free(line);
    ctl_stop(&g_ctl);
    return 0;
}
