/* mcp.h — MCP stdio server entry point. */
#ifndef BLAZE_MCP_H
#define BLAZE_MCP_H

#include <stdbool.h>
#include <stdint.h>

/* Runs the machine under an MCP server on stdin/stdout until EOF.
 * comm1/comm2 are --comm1-style config strings (NULL = loopback). */
int mcp_run(const uint8_t *rom, uint32_t rom_len, const char *nvr_path,
            const char *comm1, const char *comm2, bool skip_diagnostics);

#endif
