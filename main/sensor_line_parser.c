#include "sensor_line_parser.h"

#include <string.h>

bool sensor_line_parser_feed(sensor_line_parser_t *parser, char byte, flow_state_t *state)
{
    if (byte != '\n') {
        if (!parser->discard && parser->length < sizeof(parser->line)) {
            parser->line[parser->length++] = byte;
        } else {
            parser->discard = true;
        }
        return false;
    }
    size_t length = parser->length;
    if (length && parser->line[length - 1] == '\r') {
        --length;
    }
    bool found = false;
    if (!parser->discard) {
        if (length == 4 && memcmp(parser->line, "FLOW", 4) == 0) {
            *state = FLOW_STATE_FLOW;
            found = true;
        } else if (length == 7 && memcmp(parser->line, "NO_FLOW", 7) == 0) {
            *state = FLOW_STATE_NO_FLOW;
            found = true;
        }
    }
    parser->length = 0;
    parser->discard = false;
    return found;
}
