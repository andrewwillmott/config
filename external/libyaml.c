/*
    NOTE: this is a cut-down version of libyaml 0.2.2's source code. Please see
    https://github.com/yaml/libyaml for the full version and documentation.

    Copyright (c) 2017-2020 Ingy döt Net
    Copyright (c) 2006-2016 Kirill Simonov

    Permission is hereby granted, free of charge, to any person obtaining a copy of
    this software and associated documentation files (the "Software"), to deal in
    the Software without restriction, including without limitation the rights to
    use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
    of the Software, and to permit persons to whom the Software is furnished to do
    so, subject to the following conditions:

    The above copyright notice and this permission notice shall be included in all
    copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
    AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
    OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
    SOFTWARE.
*/

#include "yaml.h"

#include <assert.h>
#include <limits.h>
#include <stddef.h>

#ifdef _MSC_VER
    #define strdup _strdup
#endif

/*
 * Memory management.
 */

YAML_DECLARE(void *)
yaml_malloc(size_t size);

YAML_DECLARE(void *)
yaml_realloc(void *ptr, size_t size);

YAML_DECLARE(void)
yaml_free(void *ptr);

YAML_DECLARE(yaml_char_t *)
yaml_strdup(const yaml_char_t *);

/*
 * Reader: Ensure that the buffer contains at least `length` characters.
 */

YAML_DECLARE(int)
yaml_parser_update_buffer(yaml_parser_t *parser, size_t length);

/*
 * Scanner: Ensure that the token stack contains at least one token ready.
 */

YAML_DECLARE(int)
yaml_parser_fetch_more_tokens(yaml_parser_t *parser);

/*
 * The size of the input raw buffer.
 */

#define INPUT_RAW_BUFFER_SIZE   16384

/*
 * The size of the input buffer.
 *
 * It should be possible to decode the whole raw buffer.
 */

#define INPUT_BUFFER_SIZE       (INPUT_RAW_BUFFER_SIZE*3)

/*
 * The size of the output buffer.
 */

#define OUTPUT_BUFFER_SIZE      16384

/*
 * The size of the output raw buffer.
 *
 * It should be possible to encode the whole output buffer.
 */

#define OUTPUT_RAW_BUFFER_SIZE  (OUTPUT_BUFFER_SIZE*2+2)

/*
 * The maximum size of a YAML input file.
 * This used to be PTRDIFF_MAX, but that's not entirely portable
 * because stdint.h isn't available on all platforms.
 * It is not entirely clear why this isn't the maximum value
 * that can fit into the parser->offset field.
 */

#define MAX_FILE_SIZE (~(size_t)0 / 2)


/*
 * The size of other stacks and queues.
 */

#define INITIAL_STACK_SIZE  16
#define INITIAL_QUEUE_SIZE  16
#define INITIAL_STRING_SIZE 16

/*
 * Buffer management.
 */

#define BUFFER_INIT(context,buffer,size)                                        \
  (((buffer).start = (yaml_char_t *)yaml_malloc(size)) ?                        \
        ((buffer).last = (buffer).pointer = (buffer).start,                     \
         (buffer).end = (buffer).start+(size),                                  \
         1) :                                                                   \
        ((context)->error = YAML_MEMORY_ERROR,                                  \
         0))

#define BUFFER_DEL(context,buffer)                                              \
    (yaml_free((buffer).start),                                                 \
     (buffer).start = (buffer).pointer = (buffer).end = 0)

/*
 * String management.
 */

typedef struct {
    yaml_char_t *start;
    yaml_char_t *end;
    yaml_char_t *pointer;
} yaml_string_t;

YAML_DECLARE(int)
yaml_string_extend(yaml_char_t **start,
        yaml_char_t **pointer, yaml_char_t **end);

YAML_DECLARE(int)
yaml_string_join(
        yaml_char_t **a_start, yaml_char_t **a_pointer, yaml_char_t **a_end,
        yaml_char_t **b_start, yaml_char_t **b_pointer, yaml_char_t **b_end);

#define NULL_STRING { NULL, NULL, NULL }

#define STRING(string,length)   { (string), (string)+(length), (string) }

#define STRING_ASSIGN(value,string,length)                                      \
    ((value).start = (string),                                                  \
     (value).end = (string)+(length),                                           \
     (value).pointer = (string))

#define STRING_INIT(context,string,size)                                        \
    (((string).start = YAML_MALLOC(size)) ?                                     \
        ((string).pointer = (string).start,                                     \
         (string).end = (string).start+(size),                                  \
         memset((string).start, 0, (size)),                                     \
         1) :                                                                   \
        ((context)->error = YAML_MEMORY_ERROR,                                  \
         0))

#define STRING_DEL(context,string)                                              \
    (yaml_free((string).start),                                                 \
     (string).start = (string).pointer = (string).end = 0)

#define STRING_EXTEND(context,string)                                           \
    ((((string).pointer+5 < (string).end)                                       \
        || yaml_string_extend(&(string).start,                                  \
            &(string).pointer, &(string).end)) ?                                \
         1 :                                                                    \
        ((context)->error = YAML_MEMORY_ERROR,                                  \
         0))

#define CLEAR(context,string)                                                   \
    ((string).pointer = (string).start,                                         \
     memset((string).start, 0, (string).end-(string).start))

#define JOIN(context,string_a,string_b)                                         \
    ((yaml_string_join(&(string_a).start, &(string_a).pointer,                  \
                       &(string_a).end, &(string_b).start,                      \
                       &(string_b).pointer, &(string_b).end)) ?                 \
        ((string_b).pointer = (string_b).start,                                 \
         1) :                                                                   \
        ((context)->error = YAML_MEMORY_ERROR,                                  \
         0))

/*
 * String check operations.
 */

/*
 * Check the octet at the specified position.
 */

#define CHECK_AT(string,octet,offset)                   \
    ((string).pointer[offset] == (yaml_char_t)(octet))

/*
 * Check the current octet in the buffer.
 */

#define CHECK(string,octet) (CHECK_AT((string),(octet),0))

/*
 * Check if the character at the specified position is an alphabetical
 * character, a digit, '_', or '-'.
 */

#define IS_ALPHA_AT(string,offset)                                              \
     (((string).pointer[offset] >= (yaml_char_t) '0' &&                         \
       (string).pointer[offset] <= (yaml_char_t) '9') ||                        \
      ((string).pointer[offset] >= (yaml_char_t) 'A' &&                         \
       (string).pointer[offset] <= (yaml_char_t) 'Z') ||                        \
      ((string).pointer[offset] >= (yaml_char_t) 'a' &&                         \
       (string).pointer[offset] <= (yaml_char_t) 'z') ||                        \
      (string).pointer[offset] == '_' ||                                        \
      (string).pointer[offset] == '-')

#define IS_ALPHA(string)    IS_ALPHA_AT((string),0)

/*
 * Check if the character at the specified position is a digit.
 */

#define IS_DIGIT_AT(string,offset)                                              \
     (((string).pointer[offset] >= (yaml_char_t) '0' &&                         \
       (string).pointer[offset] <= (yaml_char_t) '9'))

#define IS_DIGIT(string)    IS_DIGIT_AT((string),0)

/*
 * Get the value of a digit.
 */

#define AS_DIGIT_AT(string,offset)                                              \
     ((string).pointer[offset] - (yaml_char_t) '0')

#define AS_DIGIT(string)    AS_DIGIT_AT((string),0)

/*
 * Check if the character at the specified position is a hex-digit.
 */

#define IS_HEX_AT(string,offset)                                                \
     (((string).pointer[offset] >= (yaml_char_t) '0' &&                         \
       (string).pointer[offset] <= (yaml_char_t) '9') ||                        \
      ((string).pointer[offset] >= (yaml_char_t) 'A' &&                         \
       (string).pointer[offset] <= (yaml_char_t) 'F') ||                        \
      ((string).pointer[offset] >= (yaml_char_t) 'a' &&                         \
       (string).pointer[offset] <= (yaml_char_t) 'f'))

#define IS_HEX(string)    IS_HEX_AT((string),0)

/*
 * Get the value of a hex-digit.
 */

#define AS_HEX_AT(string,offset)                                                \
      (((string).pointer[offset] >= (yaml_char_t) 'A' &&                        \
        (string).pointer[offset] <= (yaml_char_t) 'F') ?                        \
       ((string).pointer[offset] - (yaml_char_t) 'A' + 10) :                    \
       ((string).pointer[offset] >= (yaml_char_t) 'a' &&                        \
        (string).pointer[offset] <= (yaml_char_t) 'f') ?                        \
       ((string).pointer[offset] - (yaml_char_t) 'a' + 10) :                    \
       ((string).pointer[offset] - (yaml_char_t) '0'))

#define AS_HEX(string)  AS_HEX_AT((string),0)

/*
 * Check if the character is ASCII.
 */

#define IS_ASCII_AT(string,offset)                                              \
    ((string).pointer[offset] <= (yaml_char_t) '\x7F')

#define IS_ASCII(string)    IS_ASCII_AT((string),0)

/*
 * Check if the character can be printed unescaped.
 */

#define IS_PRINTABLE_AT(string,offset)                                          \
    (((string).pointer[offset] == 0x0A)         /* . == #x0A */                 \
     || ((string).pointer[offset] >= 0x20       /* #x20 <= . <= #x7E */         \
         && (string).pointer[offset] <= 0x7E)                                   \
     || ((string).pointer[offset] == 0xC2       /* #0xA0 <= . <= #xD7FF */      \
         && (string).pointer[offset+1] >= 0xA0)                                 \
     || ((string).pointer[offset] > 0xC2                                        \
         && (string).pointer[offset] < 0xED)                                    \
     || ((string).pointer[offset] == 0xED                                       \
         && (string).pointer[offset+1] < 0xA0)                                  \
     || ((string).pointer[offset] == 0xEE)                                      \
     || ((string).pointer[offset] == 0xEF      /* #xE000 <= . <= #xFFFD */      \
         && !((string).pointer[offset+1] == 0xBB        /* && . != #xFEFF */    \
             && (string).pointer[offset+2] == 0xBF)                             \
         && !((string).pointer[offset+1] == 0xBF                                \
             && ((string).pointer[offset+2] == 0xBE                             \
                 || (string).pointer[offset+2] == 0xBF))))

#define IS_PRINTABLE(string)    IS_PRINTABLE_AT((string),0)

/*
 * Check if the character at the specified position is NUL.
 */

#define IS_Z_AT(string,offset)    CHECK_AT((string),'\0',(offset))

#define IS_Z(string)    IS_Z_AT((string),0)

/*
 * Check if the character at the specified position is BOM.
 */

#define IS_BOM_AT(string,offset)                                                \
     (CHECK_AT((string),'\xEF',(offset))                                        \
      && CHECK_AT((string),'\xBB',(offset)+1)                                   \
      && CHECK_AT((string),'\xBF',(offset)+2))  /* BOM (#xFEFF) */

#define IS_BOM(string)  IS_BOM_AT(string,0)

/*
 * Check if the character at the specified position is space.
 */

#define IS_SPACE_AT(string,offset)  CHECK_AT((string),' ',(offset))

#define IS_SPACE(string)    IS_SPACE_AT((string),0)

/*
 * Check if the character at the specified position is tab.
 */

#define IS_TAB_AT(string,offset)    CHECK_AT((string),'\t',(offset))

#define IS_TAB(string)  IS_TAB_AT((string),0)

/*
 * Check if the character at the specified position is blank (space or tab).
 */

#define IS_BLANK_AT(string,offset)                                              \
    (IS_SPACE_AT((string),(offset)) || IS_TAB_AT((string),(offset)))

#define IS_BLANK(string)    IS_BLANK_AT((string),0)

/*
 * Check if the character at the specified position is a line break.
 */

#define IS_BREAK_AT(string,offset)                                              \
    (CHECK_AT((string),'\r',(offset))               /* CR (#xD)*/               \
     || CHECK_AT((string),'\n',(offset))            /* LF (#xA) */              \
     || (CHECK_AT((string),'\xC2',(offset))                                     \
         && CHECK_AT((string),'\x85',(offset)+1))   /* NEL (#x85) */            \
     || (CHECK_AT((string),'\xE2',(offset))                                     \
         && CHECK_AT((string),'\x80',(offset)+1)                                \
         && CHECK_AT((string),'\xA8',(offset)+2))   /* LS (#x2028) */           \
     || (CHECK_AT((string),'\xE2',(offset))                                     \
         && CHECK_AT((string),'\x80',(offset)+1)                                \
         && CHECK_AT((string),'\xA9',(offset)+2)))  /* PS (#x2029) */

#define IS_BREAK(string)    IS_BREAK_AT((string),0)

#define IS_CRLF_AT(string,offset)                                               \
     (CHECK_AT((string),'\r',(offset)) && CHECK_AT((string),'\n',(offset)+1))

#define IS_CRLF(string) IS_CRLF_AT((string),0)

/*
 * Check if the character is a line break or NUL.
 */

#define IS_BREAKZ_AT(string,offset)                                             \
    (IS_BREAK_AT((string),(offset)) || IS_Z_AT((string),(offset)))

#define IS_BREAKZ(string)   IS_BREAKZ_AT((string),0)

/*
 * Check if the character is a line break, space, or NUL.
 */

#define IS_SPACEZ_AT(string,offset)                                             \
    (IS_SPACE_AT((string),(offset)) || IS_BREAKZ_AT((string),(offset)))

#define IS_SPACEZ(string)   IS_SPACEZ_AT((string),0)

/*
 * Check if the character is a line break, space, tab, or NUL.
 */

#define IS_BLANKZ_AT(string,offset)                                             \
    (IS_BLANK_AT((string),(offset)) || IS_BREAKZ_AT((string),(offset)))

#define IS_BLANKZ(string)   IS_BLANKZ_AT((string),0)

/*
 * Determine the width of the character.
 */

#define WIDTH_AT(string,offset)                                                 \
     (((string).pointer[offset] & 0x80) == 0x00 ? 1 :                           \
      ((string).pointer[offset] & 0xE0) == 0xC0 ? 2 :                           \
      ((string).pointer[offset] & 0xF0) == 0xE0 ? 3 :                           \
      ((string).pointer[offset] & 0xF8) == 0xF0 ? 4 : 0)

#define WIDTH(string)   WIDTH_AT((string),0)

/*
 * Move the string pointer to the next character.
 */

#define MOVE(string)    ((string).pointer += WIDTH((string)))

/*
 * Copy a character and move the pointers of both strings.
 */

#define COPY(string_a,string_b)                                                 \
    ((*(string_b).pointer & 0x80) == 0x00 ?                                     \
     (*((string_a).pointer++) = *((string_b).pointer++)) :                      \
     (*(string_b).pointer & 0xE0) == 0xC0 ?                                     \
     (*((string_a).pointer++) = *((string_b).pointer++),                        \
      *((string_a).pointer++) = *((string_b).pointer++)) :                      \
     (*(string_b).pointer & 0xF0) == 0xE0 ?                                     \
     (*((string_a).pointer++) = *((string_b).pointer++),                        \
      *((string_a).pointer++) = *((string_b).pointer++),                        \
      *((string_a).pointer++) = *((string_b).pointer++)) :                      \
     (*(string_b).pointer & 0xF8) == 0xF0 ?                                     \
     (*((string_a).pointer++) = *((string_b).pointer++),                        \
      *((string_a).pointer++) = *((string_b).pointer++),                        \
      *((string_a).pointer++) = *((string_b).pointer++),                        \
      *((string_a).pointer++) = *((string_b).pointer++)) : 0)

/*
 * Stack and queue management.
 */

YAML_DECLARE(int)
yaml_stack_extend(void **start, void **top, void **end);

YAML_DECLARE(int)
yaml_queue_extend(void **start, void **head, void **tail, void **end);

#define STACK_INIT(context,stack,type)                                     \
  (((stack).start = (type)yaml_malloc(INITIAL_STACK_SIZE*sizeof(*(stack).start))) ? \
        ((stack).top = (stack).start,                                           \
         (stack).end = (stack).start+INITIAL_STACK_SIZE,                        \
         1) :                                                                   \
        ((context)->error = YAML_MEMORY_ERROR,                                  \
         0))

#define STACK_DEL(context,stack)                                                \
    (yaml_free((stack).start),                                                  \
     (stack).start = (stack).top = (stack).end = 0)

#define STACK_EMPTY(context,stack)                                              \
    ((stack).start == (stack).top)

#define STACK_LIMIT(context,stack,size)                                         \
    ((stack).top - (stack).start < (size) ?                                     \
        1 :                                                                     \
        ((context)->error = YAML_MEMORY_ERROR,                                  \
         0))

#define PUSH(context,stack,value)                                               \
    (((stack).top != (stack).end                                                \
      || yaml_stack_extend((void **)&(stack).start,                             \
              (void **)&(stack).top, (void **)&(stack).end)) ?                  \
        (*((stack).top++) = value,                                              \
         1) :                                                                   \
        ((context)->error = YAML_MEMORY_ERROR,                                  \
         0))

#define POP(context,stack)                                                      \
    (*(--(stack).top))

#define QUEUE_INIT(context,queue,size,type)                                     \
  (((queue).start = (type)yaml_malloc((size)*sizeof(*(queue).start))) ?         \
        ((queue).head = (queue).tail = (queue).start,                           \
         (queue).end = (queue).start+(size),                                    \
         1) :                                                                   \
        ((context)->error = YAML_MEMORY_ERROR,                                  \
         0))

#define QUEUE_DEL(context,queue)                                                \
    (yaml_free((queue).start),                                                  \
     (queue).start = (queue).head = (queue).tail = (queue).end = 0)

#define QUEUE_EMPTY(context,queue)                                              \
    ((queue).head == (queue).tail)

#define ENQUEUE(context,queue,value)                                            \
    (((queue).tail != (queue).end                                               \
      || yaml_queue_extend((void **)&(queue).start, (void **)&(queue).head,     \
            (void **)&(queue).tail, (void **)&(queue).end)) ?                   \
        (*((queue).tail++) = value,                                             \
         1) :                                                                   \
        ((context)->error = YAML_MEMORY_ERROR,                                  \
         0))

#define DEQUEUE(context,queue)                                                  \
    (*((queue).head++))

#define QUEUE_INSERT(context,queue,index,value)                                 \
    (((queue).tail != (queue).end                                               \
      || yaml_queue_extend((void **)&(queue).start, (void **)&(queue).head,     \
            (void **)&(queue).tail, (void **)&(queue).end)) ?                   \
        (memmove((queue).head+(index)+1,(queue).head+(index),                   \
            ((queue).tail-(queue).head-(index))*sizeof(*(queue).start)),        \
         *((queue).head+(index)) = value,                                       \
         (queue).tail++,                                                        \
         1) :                                                                   \
        ((context)->error = YAML_MEMORY_ERROR,                                  \
         0))

/*
 * Token initializers.
 */

#define TOKEN_INIT(token,token_type,token_start_mark,token_end_mark)            \
    (memset(&(token), 0, sizeof(yaml_token_t)),                                 \
     (token).type = (token_type),                                               \
     (token).start_mark = (token_start_mark),                                   \
     (token).end_mark = (token_end_mark))

#define STREAM_START_TOKEN_INIT(token,token_encoding,start_mark,end_mark)       \
    (TOKEN_INIT((token),YAML_STREAM_START_TOKEN,(start_mark),(end_mark)),       \
     (token).data.stream_start.encoding = (token_encoding))

#define STREAM_END_TOKEN_INIT(token,start_mark,end_mark)                        \
    (TOKEN_INIT((token),YAML_STREAM_END_TOKEN,(start_mark),(end_mark)))

#define ALIAS_TOKEN_INIT(token,token_value,start_mark,end_mark)                 \
    (TOKEN_INIT((token),YAML_ALIAS_TOKEN,(start_mark),(end_mark)),              \
     (token).data.alias.value = (token_value))

#define ANCHOR_TOKEN_INIT(token,token_value,start_mark,end_mark)                \
    (TOKEN_INIT((token),YAML_ANCHOR_TOKEN,(start_mark),(end_mark)),             \
     (token).data.anchor.value = (token_value))

#define TAG_TOKEN_INIT(token,token_handle,token_suffix,start_mark,end_mark)     \
    (TOKEN_INIT((token),YAML_TAG_TOKEN,(start_mark),(end_mark)),                \
     (token).data.tag.handle = (token_handle),                                  \
     (token).data.tag.suffix = (token_suffix))

#define SCALAR_TOKEN_INIT(token,token_value,token_length,token_style,start_mark,end_mark)   \
    (TOKEN_INIT((token),YAML_SCALAR_TOKEN,(start_mark),(end_mark)),             \
     (token).data.scalar.value = (token_value),                                 \
     (token).data.scalar.length = (token_length),                               \
     (token).data.scalar.style = (token_style))

#define VERSION_DIRECTIVE_TOKEN_INIT(token,token_major,token_minor,start_mark,end_mark)     \
    (TOKEN_INIT((token),YAML_VERSION_DIRECTIVE_TOKEN,(start_mark),(end_mark)),  \
     (token).data.version_directive.major = (token_major),                      \
     (token).data.version_directive.minor = (token_minor))

#define TAG_DIRECTIVE_TOKEN_INIT(token,token_handle,token_prefix,start_mark,end_mark)       \
    (TOKEN_INIT((token),YAML_TAG_DIRECTIVE_TOKEN,(start_mark),(end_mark)),      \
     (token).data.tag_directive.handle = (token_handle),                        \
     (token).data.tag_directive.prefix = (token_prefix))

/*
 * Event initializers.
 */

#define EVENT_INIT(event,event_type,event_start_mark,event_end_mark)            \
    (memset(&(event), 0, sizeof(yaml_event_t)),                                 \
     (event).type = (event_type),                                               \
     (event).start_mark = (event_start_mark),                                   \
     (event).end_mark = (event_end_mark))

#define STREAM_START_EVENT_INIT(event,event_encoding,start_mark,end_mark)       \
    (EVENT_INIT((event),YAML_STREAM_START_EVENT,(start_mark),(end_mark)),       \
     (event).data.stream_start.encoding = (event_encoding))

#define STREAM_END_EVENT_INIT(event,start_mark,end_mark)                        \
    (EVENT_INIT((event),YAML_STREAM_END_EVENT,(start_mark),(end_mark)))

#define DOCUMENT_START_EVENT_INIT(event,event_version_directive,                \
        event_tag_directives_start,event_tag_directives_end,event_implicit,start_mark,end_mark) \
    (EVENT_INIT((event),YAML_DOCUMENT_START_EVENT,(start_mark),(end_mark)),     \
     (event).data.document_start.version_directive = (event_version_directive), \
     (event).data.document_start.tag_directives.start = (event_tag_directives_start),   \
     (event).data.document_start.tag_directives.end = (event_tag_directives_end),   \
     (event).data.document_start.implicit = (event_implicit))

#define DOCUMENT_END_EVENT_INIT(event,event_implicit,start_mark,end_mark)       \
    (EVENT_INIT((event),YAML_DOCUMENT_END_EVENT,(start_mark),(end_mark)),       \
     (event).data.document_end.implicit = (event_implicit))

#define ALIAS_EVENT_INIT(event,event_anchor,start_mark,end_mark)                \
    (EVENT_INIT((event),YAML_ALIAS_EVENT,(start_mark),(end_mark)),              \
     (event).data.alias.anchor = (event_anchor))

#define SCALAR_EVENT_INIT(event,event_anchor,event_tag,event_value,event_length,    \
        event_plain_implicit, event_quoted_implicit,event_style,start_mark,end_mark)    \
    (EVENT_INIT((event),YAML_SCALAR_EVENT,(start_mark),(end_mark)),             \
     (event).data.scalar.anchor = (event_anchor),                               \
     (event).data.scalar.tag = (event_tag),                                     \
     (event).data.scalar.value = (event_value),                                 \
     (event).data.scalar.length = (event_length),                               \
     (event).data.scalar.plain_implicit = (event_plain_implicit),               \
     (event).data.scalar.quoted_implicit = (event_quoted_implicit),             \
     (event).data.scalar.style = (event_style))

#define SEQUENCE_START_EVENT_INIT(event,event_anchor,event_tag,                 \
        event_implicit,event_style,start_mark,end_mark)                         \
    (EVENT_INIT((event),YAML_SEQUENCE_START_EVENT,(start_mark),(end_mark)),     \
     (event).data.sequence_start.anchor = (event_anchor),                       \
     (event).data.sequence_start.tag = (event_tag),                             \
     (event).data.sequence_start.implicit = (event_implicit),                   \
     (event).data.sequence_start.style = (event_style))

#define SEQUENCE_END_EVENT_INIT(event,start_mark,end_mark)                      \
    (EVENT_INIT((event),YAML_SEQUENCE_END_EVENT,(start_mark),(end_mark)))

#define MAPPING_START_EVENT_INIT(event,event_anchor,event_tag,                  \
        event_implicit,event_style,start_mark,end_mark)                         \
    (EVENT_INIT((event),YAML_MAPPING_START_EVENT,(start_mark),(end_mark)),      \
     (event).data.mapping_start.anchor = (event_anchor),                        \
     (event).data.mapping_start.tag = (event_tag),                              \
     (event).data.mapping_start.implicit = (event_implicit),                    \
     (event).data.mapping_start.style = (event_style))

#define MAPPING_END_EVENT_INIT(event,start_mark,end_mark)                       \
    (EVENT_INIT((event),YAML_MAPPING_END_EVENT,(start_mark),(end_mark)))

/*
 * Document initializer.
 */

#define DOCUMENT_INIT(document,document_nodes_start,document_nodes_end,         \
        document_version_directive,document_tag_directives_start,               \
        document_tag_directives_end,document_start_implicit,                    \
        document_end_implicit,document_start_mark,document_end_mark)            \
    (memset(&(document), 0, sizeof(yaml_document_t)),                           \
     (document).nodes.start = (document_nodes_start),                           \
     (document).nodes.end = (document_nodes_end),                               \
     (document).nodes.top = (document_nodes_start),                             \
     (document).version_directive = (document_version_directive),               \
     (document).tag_directives.start = (document_tag_directives_start),         \
     (document).tag_directives.end = (document_tag_directives_end),             \
     (document).start_implicit = (document_start_implicit),                     \
     (document).end_implicit = (document_end_implicit),                         \
     (document).start_mark = (document_start_mark),                             \
     (document).end_mark = (document_end_mark))

/*
 * Node initializers.
 */

#define NODE_INIT(node,node_type,node_tag,node_start_mark,node_end_mark)        \
    (memset(&(node), 0, sizeof(yaml_node_t)),                                   \
     (node).type = (node_type),                                                 \
     (node).tag = (node_tag),                                                   \
     (node).start_mark = (node_start_mark),                                     \
     (node).end_mark = (node_end_mark))

#define SCALAR_NODE_INIT(node,node_tag,node_value,node_length,                  \
        node_style,start_mark,end_mark)                                         \
    (NODE_INIT((node),YAML_SCALAR_NODE,(node_tag),(start_mark),(end_mark)),     \
     (node).data.scalar.value = (node_value),                                   \
     (node).data.scalar.length = (node_length),                                 \
     (node).data.scalar.style = (node_style))

#define SEQUENCE_NODE_INIT(node,node_tag,node_items_start,node_items_end,       \
        node_style,start_mark,end_mark)                                         \
    (NODE_INIT((node),YAML_SEQUENCE_NODE,(node_tag),(start_mark),(end_mark)),   \
     (node).data.sequence.items.start = (node_items_start),                     \
     (node).data.sequence.items.end = (node_items_end),                         \
     (node).data.sequence.items.top = (node_items_start),                       \
     (node).data.sequence.style = (node_style))

#define MAPPING_NODE_INIT(node,node_tag,node_pairs_start,node_pairs_end,        \
        node_style,start_mark,end_mark)                                         \
    (NODE_INIT((node),YAML_MAPPING_NODE,(node_tag),(start_mark),(end_mark)),    \
     (node).data.mapping.pairs.start = (node_pairs_start),                      \
     (node).data.mapping.pairs.end = (node_pairs_end),                          \
     (node).data.mapping.pairs.top = (node_pairs_start),                        \
     (node).data.mapping.style = (node_style))

/* Strict C compiler warning helpers */

#if defined(__clang__) || defined(__GNUC__)
#  define HASATTRIBUTE_UNUSED
#endif
#ifdef HASATTRIBUTE_UNUSED
#  define __attribute__unused__             __attribute__((__unused__))
#else
#  define __attribute__unused__
#endif

/* Shim arguments are arguments that must be included in your function,
 * but serve no purpose inside.  Silence compiler warnings. */
#define SHIM(a) /*@unused@*/ a __attribute__unused__

/* UNUSED_PARAM() marks a shim argument in the body to silence compiler warnings */
#ifdef __clang__
#  define UNUSED_PARAM(a) (void)(a);
#else
#  define UNUSED_PARAM(a) /*@-noeffect*/if (0) (void)(a)/*@=noeffect*/;
#endif

#define YAML_MALLOC_STATIC(type) (type*)yaml_malloc(sizeof(type))
#define YAML_MALLOC(size)        (yaml_char_t *)yaml_malloc(size)

#ifndef HL_ALLOC

/*
 * Allocate a dynamic memory block.
 */

YAML_DECLARE(void *)
yaml_malloc(size_t size)
{
    return malloc(size ? size : 1);
}

/*
 * Reallocate a dynamic memory block.
 */

YAML_DECLARE(void *)
yaml_realloc(void *ptr, size_t size)
{
    return ptr ? realloc(ptr, size ? size : 1) : malloc(size ? size : 1);
}

/*
 * Free a dynamic memory block.
 */

YAML_DECLARE(void)
yaml_free(void *ptr)
{
    if (ptr) free(ptr);
}

/*
 * Duplicate a string.
 */

YAML_DECLARE(yaml_char_t *)
yaml_strdup(const yaml_char_t *str)
{
    if (!str)
        return NULL;

    return (yaml_char_t *)strdup((char *)str);
}

#endif

/*
 * Extend a string.
 */

YAML_DECLARE(int)
yaml_string_extend(yaml_char_t **start,
        yaml_char_t **pointer, yaml_char_t **end)
{
    yaml_char_t *new_start = (yaml_char_t *)yaml_realloc((void*)*start, (*end - *start)*2);

    if (!new_start) return 0;

    memset(new_start + (*end - *start), 0, *end - *start);

    *pointer = new_start + (*pointer - *start);
    *end = new_start + (*end - *start)*2;
    *start = new_start;

    return 1;
}

/*
 * Append a string B to a string A.
 */

YAML_DECLARE(int)
yaml_string_join(
        yaml_char_t **a_start, yaml_char_t **a_pointer, yaml_char_t **a_end,
        yaml_char_t **b_start, yaml_char_t **b_pointer, SHIM(yaml_char_t **b_end))
{
    UNUSED_PARAM(b_end)
    if (*b_start == *b_pointer)
        return 1;

    while (*a_end - *a_pointer <= *b_pointer - *b_start) {
        if (!yaml_string_extend(a_start, a_pointer, a_end))
            return 0;
    }

    memcpy(*a_pointer, *b_start, *b_pointer - *b_start);
    *a_pointer += *b_pointer - *b_start;

    return 1;
}

/*
 * Extend a stack.
 */

YAML_DECLARE(int)
yaml_stack_extend(void **start, void **top, void **end)
{
    void *new_start;

    if ((char *)*end - (char *)*start >= INT_MAX / 2)
	return 0;

    new_start = yaml_realloc(*start, ((char *)*end - (char *)*start)*2);

    if (!new_start) return 0;

    *top = (char *)new_start + ((char *)*top - (char *)*start);
    *end = (char *)new_start + ((char *)*end - (char *)*start)*2;
    *start = new_start;

    return 1;
}

/*
 * Extend or move a queue.
 */

YAML_DECLARE(int)
yaml_queue_extend(void **start, void **head, void **tail, void **end)
{
    /* Check if we need to resize the queue. */

    if (*start == *head && *tail == *end) {
        void *new_start = yaml_realloc(*start,
                ((char *)*end - (char *)*start)*2);

        if (!new_start) return 0;

        *head = (char *)new_start + ((char *)*head - (char *)*start);
        *tail = (char *)new_start + ((char *)*tail - (char *)*start);
        *end = (char *)new_start + ((char *)*end - (char *)*start)*2;
        *start = new_start;
    }

    /* Check if we need to move the queue at the beginning of the buffer. */

    if (*tail == *end) {
        if (*head != *tail) {
            memmove(*start, *head, (char *)*tail - (char *)*head);
        }
        *tail = (char *)*tail - (char *)*head + (char *)*start;
        *head = *start;
    }

    return 1;
}


/*
 * Create a new parser object.
 */

YAML_DECLARE(int)
yaml_parser_initialize(yaml_parser_t *parser)
{
    assert(parser);     /* Non-NULL parser object expected. */

    memset(parser, 0, sizeof(yaml_parser_t));
    if (!BUFFER_INIT(parser, parser->raw_buffer, INPUT_RAW_BUFFER_SIZE))
        goto error;
    if (!BUFFER_INIT(parser, parser->buffer, INPUT_BUFFER_SIZE))
        goto error;
    if (!QUEUE_INIT(parser, parser->tokens, INITIAL_QUEUE_SIZE, yaml_token_t*))
        goto error;
    if (!STACK_INIT(parser, parser->indents, int*))
        goto error;
    if (!STACK_INIT(parser, parser->simple_keys, yaml_simple_key_t*))
        goto error;
    if (!STACK_INIT(parser, parser->states, yaml_parser_state_t*))
        goto error;
    if (!STACK_INIT(parser, parser->marks, yaml_mark_t*))
        goto error;
    if (!STACK_INIT(parser, parser->tag_directives, yaml_tag_directive_t*))
        goto error;

    return 1;

error:

    BUFFER_DEL(parser, parser->raw_buffer);
    BUFFER_DEL(parser, parser->buffer);
    QUEUE_DEL(parser, parser->tokens);
    STACK_DEL(parser, parser->indents);
    STACK_DEL(parser, parser->simple_keys);
    STACK_DEL(parser, parser->states);
    STACK_DEL(parser, parser->marks);
    STACK_DEL(parser, parser->tag_directives);

    return 0;
}

/*
 * Destroy a parser object.
 */

YAML_DECLARE(void)
yaml_parser_delete(yaml_parser_t *parser)
{
    assert(parser); /* Non-NULL parser object expected. */

    BUFFER_DEL(parser, parser->raw_buffer);
    BUFFER_DEL(parser, parser->buffer);
    while (!QUEUE_EMPTY(parser, parser->tokens)) {
        yaml_token_delete(&DEQUEUE(parser, parser->tokens));
    }
    QUEUE_DEL(parser, parser->tokens);
    STACK_DEL(parser, parser->indents);
    STACK_DEL(parser, parser->simple_keys);
    STACK_DEL(parser, parser->states);
    STACK_DEL(parser, parser->marks);
    while (!STACK_EMPTY(parser, parser->tag_directives)) {
        yaml_tag_directive_t tag_directive = POP(parser, parser->tag_directives);
        yaml_free(tag_directive.handle);
        yaml_free(tag_directive.prefix);
    }
    STACK_DEL(parser, parser->tag_directives);

    memset(parser, 0, sizeof(yaml_parser_t));
}

/*
 * String read handler.
 */

static int
yaml_string_read_handler(void *data, unsigned char *buffer, size_t size,
        size_t *size_read)
{
    yaml_parser_t *parser = (yaml_parser_t *)data;

    if (parser->input.string.current == parser->input.string.end) {
        *size_read = 0;
        return 1;
    }

    if (size > (size_t)(parser->input.string.end
                - parser->input.string.current)) {
        size = parser->input.string.end - parser->input.string.current;
    }

    memcpy(buffer, parser->input.string.current, size);
    parser->input.string.current += size;
    *size_read = size;
    return 1;
}

/*
 * File read handler.
 */

static int
yaml_file_read_handler(void *data, unsigned char *buffer, size_t size,
        size_t *size_read)
{
    yaml_parser_t *parser = (yaml_parser_t *)data;

    *size_read = fread(buffer, 1, size, parser->input.file);
    return !ferror(parser->input.file);
}

/*
 * Set a string input.
 */

YAML_DECLARE(void)
yaml_parser_set_input_string(yaml_parser_t *parser,
        const unsigned char *input, size_t size)
{
    assert(parser); /* Non-NULL parser object expected. */
    assert(!parser->read_handler);  /* You can set the source only once. */
    assert(input);  /* Non-NULL input string expected. */

    parser->read_handler = yaml_string_read_handler;
    parser->read_handler_data = parser;

    parser->input.string.start = input;
    parser->input.string.current = input;
    parser->input.string.end = input+size;
}

/*
 * Set a file input.
 */

YAML_DECLARE(void)
yaml_parser_set_input_file(yaml_parser_t *parser, FILE *file)
{
    assert(parser); /* Non-NULL parser object expected. */
    assert(!parser->read_handler);  /* You can set the source only once. */
    assert(file);   /* Non-NULL file object expected. */

    parser->read_handler = yaml_file_read_handler;
    parser->read_handler_data = parser;

    parser->input.file = file;
}

/*
 * Set a generic input.
 */

YAML_DECLARE(void)
yaml_parser_set_input(yaml_parser_t *parser,
        yaml_read_handler_t *handler, void *data)
{
    assert(parser); /* Non-NULL parser object expected. */
    assert(!parser->read_handler);  /* You can set the source only once. */
    assert(handler);    /* Non-NULL read handler expected. */

    parser->read_handler = handler;
    parser->read_handler_data = data;
}

/*
 * Set the source encoding.
 */

YAML_DECLARE(void)
yaml_parser_set_encoding(yaml_parser_t *parser, yaml_encoding_t encoding)
{
    assert(parser); /* Non-NULL parser object expected. */
    assert(!parser->encoding); /* Encoding is already set or detected. */

    parser->encoding = encoding;
}

/*
 * Destroy a token object.
 */

YAML_DECLARE(void)
yaml_token_delete(yaml_token_t *token)
{
    assert(token);  /* Non-NULL token object expected. */

    switch (token->type)
    {
        case YAML_TAG_DIRECTIVE_TOKEN:
            yaml_free(token->data.tag_directive.handle);
            yaml_free(token->data.tag_directive.prefix);
            break;

        case YAML_ALIAS_TOKEN:
            yaml_free(token->data.alias.value);
            break;

        case YAML_ANCHOR_TOKEN:
            yaml_free(token->data.anchor.value);
            break;

        case YAML_TAG_TOKEN:
            yaml_free(token->data.tag.handle);
            yaml_free(token->data.tag.suffix);
            break;

        case YAML_SCALAR_TOKEN:
            yaml_free(token->data.scalar.value);
            break;

        default:
            break;
    }

    memset(token, 0, sizeof(yaml_token_t));
}

/*
 * Destroy an event object.
 */

YAML_DECLARE(void)
yaml_event_delete(yaml_event_t *event)
{
    yaml_tag_directive_t *tag_directive;

    assert(event);  /* Non-NULL event object expected. */

    switch (event->type)
    {
        case YAML_DOCUMENT_START_EVENT:
            yaml_free(event->data.document_start.version_directive);
            for (tag_directive = event->data.document_start.tag_directives.start;
                    tag_directive != event->data.document_start.tag_directives.end;
                    tag_directive++) {
                yaml_free(tag_directive->handle);
                yaml_free(tag_directive->prefix);
            }
            yaml_free(event->data.document_start.tag_directives.start);
            break;

        case YAML_ALIAS_EVENT:
            yaml_free(event->data.alias.anchor);
            break;

        case YAML_SCALAR_EVENT:
            yaml_free(event->data.scalar.anchor);
            yaml_free(event->data.scalar.tag);
            yaml_free(event->data.scalar.value);
            break;

        case YAML_SEQUENCE_START_EVENT:
            yaml_free(event->data.sequence_start.anchor);
            yaml_free(event->data.sequence_start.tag);
            break;

        case YAML_MAPPING_START_EVENT:
            yaml_free(event->data.mapping_start.anchor);
            yaml_free(event->data.mapping_start.tag);
            break;

        default:
            break;
    }

    memset(event, 0, sizeof(yaml_event_t));
}



/*
 * The parser implements the following grammar:
 *
 * stream               ::= STREAM-START implicit_document? explicit_document* STREAM-END
 * implicit_document    ::= block_node DOCUMENT-END*
 * explicit_document    ::= DIRECTIVE* DOCUMENT-START block_node? DOCUMENT-END*
 * block_node_or_indentless_sequence    ::=
 *                          ALIAS
 *                          | properties (block_content | indentless_block_sequence)?
 *                          | block_content
 *                          | indentless_block_sequence
 * block_node           ::= ALIAS
 *                          | properties block_content?
 *                          | block_content
 * flow_node            ::= ALIAS
 *                          | properties flow_content?
 *                          | flow_content
 * properties           ::= TAG ANCHOR? | ANCHOR TAG?
 * block_content        ::= block_collection | flow_collection | SCALAR
 * flow_content         ::= flow_collection | SCALAR
 * block_collection     ::= block_sequence | block_mapping
 * flow_collection      ::= flow_sequence | flow_mapping
 * block_sequence       ::= BLOCK-SEQUENCE-START (BLOCK-ENTRY block_node?)* BLOCK-END
 * indentless_sequence  ::= (BLOCK-ENTRY block_node?)+
 * block_mapping        ::= BLOCK-MAPPING_START
 *                          ((KEY block_node_or_indentless_sequence?)?
 *                          (VALUE block_node_or_indentless_sequence?)?)*
 *                          BLOCK-END
 * flow_sequence        ::= FLOW-SEQUENCE-START
 *                          (flow_sequence_entry FLOW-ENTRY)*
 *                          flow_sequence_entry?
 *                          FLOW-SEQUENCE-END
 * flow_sequence_entry  ::= flow_node | KEY flow_node? (VALUE flow_node?)?
 * flow_mapping         ::= FLOW-MAPPING-START
 *                          (flow_mapping_entry FLOW-ENTRY)*
 *                          flow_mapping_entry?
 *                          FLOW-MAPPING-END
 * flow_mapping_entry   ::= flow_node | KEY flow_node? (VALUE flow_node?)?
 */

/*
 * Peek the next token in the token queue.
 */

#define PEEK_TOKEN(parser)                                                      \
    ((parser->token_available || yaml_parser_fetch_more_tokens(parser)) ?       \
        parser->tokens.head : NULL)

/*
 * Remove the next token from the queue (must be called after PEEK_TOKEN).
 */

#define SKIP_TOKEN(parser)                                                      \
    (parser->token_available = 0,                                               \
     parser->tokens_parsed ++,                                                  \
     parser->stream_end_produced =                                              \
        (parser->tokens.head->type == YAML_STREAM_END_TOKEN),                   \
     parser->tokens.head ++)

/*
 * Public API declarations.
 */

YAML_DECLARE(int)
yaml_parser_parse(yaml_parser_t *parser, yaml_event_t *event);

/*
 * Error handling.
 */

static int
yaml_parser_set_parser_error(yaml_parser_t *parser,
        const char *problem, yaml_mark_t problem_mark);

static int
yaml_parser_set_parser_error_context(yaml_parser_t *parser,
        const char *context, yaml_mark_t context_mark,
        const char *problem, yaml_mark_t problem_mark);

/*
 * State functions.
 */

static int
yaml_parser_state_machine(yaml_parser_t *parser, yaml_event_t *event);

static int
yaml_parser_parse_stream_start(yaml_parser_t *parser, yaml_event_t *event);

static int
yaml_parser_parse_document_start(yaml_parser_t *parser, yaml_event_t *event,
        int implicit);

static int
yaml_parser_parse_document_content(yaml_parser_t *parser, yaml_event_t *event);

static int
yaml_parser_parse_document_end(yaml_parser_t *parser, yaml_event_t *event);

static int
yaml_parser_parse_node(yaml_parser_t *parser, yaml_event_t *event,
        int block, int indentless_sequence);

static int
yaml_parser_parse_block_sequence_entry(yaml_parser_t *parser,
        yaml_event_t *event, int first);

static int
yaml_parser_parse_indentless_sequence_entry(yaml_parser_t *parser,
        yaml_event_t *event);

static int
yaml_parser_parse_block_mapping_key(yaml_parser_t *parser,
        yaml_event_t *event, int first);

static int
yaml_parser_parse_block_mapping_value(yaml_parser_t *parser,
        yaml_event_t *event);

static int
yaml_parser_parse_flow_sequence_entry(yaml_parser_t *parser,
        yaml_event_t *event, int first);

static int
yaml_parser_parse_flow_sequence_entry_mapping_key(yaml_parser_t *parser,
        yaml_event_t *event);

static int
yaml_parser_parse_flow_sequence_entry_mapping_value(yaml_parser_t *parser,
        yaml_event_t *event);

static int
yaml_parser_parse_flow_sequence_entry_mapping_end(yaml_parser_t *parser,
        yaml_event_t *event);

static int
yaml_parser_parse_flow_mapping_key(yaml_parser_t *parser,
        yaml_event_t *event, int first);

static int
yaml_parser_parse_flow_mapping_value(yaml_parser_t *parser,
        yaml_event_t *event, int empty);

/*
 * Utility functions.
 */

static int
yaml_parser_process_empty_scalar(yaml_parser_t *parser,
        yaml_event_t *event, yaml_mark_t mark);

static int
yaml_parser_process_directives(yaml_parser_t *parser,
        yaml_version_directive_t **version_directive_ref,
        yaml_tag_directive_t **tag_directives_start_ref,
        yaml_tag_directive_t **tag_directives_end_ref);

static int
yaml_parser_append_tag_directive(yaml_parser_t *parser,
        yaml_tag_directive_t value, int allow_duplicates, yaml_mark_t mark);

/*
 * Get the next event.
 */

YAML_DECLARE(int)
yaml_parser_parse(yaml_parser_t *parser, yaml_event_t *event)
{
    assert(parser);     /* Non-NULL parser object is expected. */
    assert(event);      /* Non-NULL event object is expected. */

    /* Erase the event object. */

    memset(event, 0, sizeof(yaml_event_t));

    /* No events after the end of the stream or error. */

    if (parser->stream_end_produced || parser->error ||
            parser->state == YAML_PARSE_END_STATE) {
        return 1;
    }

    /* Generate the next event. */

    return yaml_parser_state_machine(parser, event);
}

/*
 * Set parser error.
 */

static int
yaml_parser_set_parser_error(yaml_parser_t *parser,
        const char *problem, yaml_mark_t problem_mark)
{
    parser->error = YAML_PARSER_ERROR;
    parser->problem = problem;
    parser->problem_mark = problem_mark;

    return 0;
}

static int
yaml_parser_set_parser_error_context(yaml_parser_t *parser,
        const char *context, yaml_mark_t context_mark,
        const char *problem, yaml_mark_t problem_mark)
{
    parser->error = YAML_PARSER_ERROR;
    parser->context = context;
    parser->context_mark = context_mark;
    parser->problem = problem;
    parser->problem_mark = problem_mark;

    return 0;
}


/*
 * State dispatcher.
 */

static int
yaml_parser_state_machine(yaml_parser_t *parser, yaml_event_t *event)
{
    switch (parser->state)
    {
        case YAML_PARSE_STREAM_START_STATE:
            return yaml_parser_parse_stream_start(parser, event);

        case YAML_PARSE_IMPLICIT_DOCUMENT_START_STATE:
            return yaml_parser_parse_document_start(parser, event, 1);

        case YAML_PARSE_DOCUMENT_START_STATE:
            return yaml_parser_parse_document_start(parser, event, 0);

        case YAML_PARSE_DOCUMENT_CONTENT_STATE:
            return yaml_parser_parse_document_content(parser, event);

        case YAML_PARSE_DOCUMENT_END_STATE:
            return yaml_parser_parse_document_end(parser, event);

        case YAML_PARSE_BLOCK_NODE_STATE:
            return yaml_parser_parse_node(parser, event, 1, 0);

        case YAML_PARSE_BLOCK_NODE_OR_INDENTLESS_SEQUENCE_STATE:
            return yaml_parser_parse_node(parser, event, 1, 1);

        case YAML_PARSE_FLOW_NODE_STATE:
            return yaml_parser_parse_node(parser, event, 0, 0);

        case YAML_PARSE_BLOCK_SEQUENCE_FIRST_ENTRY_STATE:
            return yaml_parser_parse_block_sequence_entry(parser, event, 1);

        case YAML_PARSE_BLOCK_SEQUENCE_ENTRY_STATE:
            return yaml_parser_parse_block_sequence_entry(parser, event, 0);

        case YAML_PARSE_INDENTLESS_SEQUENCE_ENTRY_STATE:
            return yaml_parser_parse_indentless_sequence_entry(parser, event);

        case YAML_PARSE_BLOCK_MAPPING_FIRST_KEY_STATE:
            return yaml_parser_parse_block_mapping_key(parser, event, 1);

        case YAML_PARSE_BLOCK_MAPPING_KEY_STATE:
            return yaml_parser_parse_block_mapping_key(parser, event, 0);

        case YAML_PARSE_BLOCK_MAPPING_VALUE_STATE:
            return yaml_parser_parse_block_mapping_value(parser, event);

        case YAML_PARSE_FLOW_SEQUENCE_FIRST_ENTRY_STATE:
            return yaml_parser_parse_flow_sequence_entry(parser, event, 1);

        case YAML_PARSE_FLOW_SEQUENCE_ENTRY_STATE:
            return yaml_parser_parse_flow_sequence_entry(parser, event, 0);

        case YAML_PARSE_FLOW_SEQUENCE_ENTRY_MAPPING_KEY_STATE:
            return yaml_parser_parse_flow_sequence_entry_mapping_key(parser, event);

        case YAML_PARSE_FLOW_SEQUENCE_ENTRY_MAPPING_VALUE_STATE:
            return yaml_parser_parse_flow_sequence_entry_mapping_value(parser, event);

        case YAML_PARSE_FLOW_SEQUENCE_ENTRY_MAPPING_END_STATE:
            return yaml_parser_parse_flow_sequence_entry_mapping_end(parser, event);

        case YAML_PARSE_FLOW_MAPPING_FIRST_KEY_STATE:
            return yaml_parser_parse_flow_mapping_key(parser, event, 1);

        case YAML_PARSE_FLOW_MAPPING_KEY_STATE:
            return yaml_parser_parse_flow_mapping_key(parser, event, 0);

        case YAML_PARSE_FLOW_MAPPING_VALUE_STATE:
            return yaml_parser_parse_flow_mapping_value(parser, event, 0);

        case YAML_PARSE_FLOW_MAPPING_EMPTY_VALUE_STATE:
            return yaml_parser_parse_flow_mapping_value(parser, event, 1);

        default:
            assert(1);      /* Invalid state. */
    }

    return 0;
}

/*
 * Parse the production:
 * stream   ::= STREAM-START implicit_document? explicit_document* STREAM-END
 *              ************
 */

static int
yaml_parser_parse_stream_start(yaml_parser_t *parser, yaml_event_t *event)
{
    yaml_token_t *token;

    token = PEEK_TOKEN(parser);
    if (!token) return 0;

    if (token->type != YAML_STREAM_START_TOKEN) {
        return yaml_parser_set_parser_error(parser,
                "did not find expected <stream-start>", token->start_mark);
    }

    parser->state = YAML_PARSE_IMPLICIT_DOCUMENT_START_STATE;
    STREAM_START_EVENT_INIT(*event, token->data.stream_start.encoding,
            token->start_mark, token->start_mark);
    SKIP_TOKEN(parser);

    return 1;
}

/*
 * Parse the productions:
 * implicit_document    ::= block_node DOCUMENT-END*
 *                          *
 * explicit_document    ::= DIRECTIVE* DOCUMENT-START block_node? DOCUMENT-END*
 *                          *************************
 */

static int
yaml_parser_parse_document_start(yaml_parser_t *parser, yaml_event_t *event,
        int implicit)
{
    yaml_token_t *token;
    yaml_version_directive_t *version_directive = NULL;
    struct {
        yaml_tag_directive_t *start;
        yaml_tag_directive_t *end;
    } tag_directives = { NULL, NULL };

    token = PEEK_TOKEN(parser);
    if (!token) return 0;

    /* Parse extra document end indicators. */

    if (!implicit)
    {
        while (token->type == YAML_DOCUMENT_END_TOKEN) {
            SKIP_TOKEN(parser);
            token = PEEK_TOKEN(parser);
            if (!token) return 0;
        }
    }

    /* Parse an implicit document. */

    if (implicit && token->type != YAML_VERSION_DIRECTIVE_TOKEN &&
            token->type != YAML_TAG_DIRECTIVE_TOKEN &&
            token->type != YAML_DOCUMENT_START_TOKEN &&
            token->type != YAML_STREAM_END_TOKEN)
    {
        if (!yaml_parser_process_directives(parser, NULL, NULL, NULL))
            return 0;
        if (!PUSH(parser, parser->states, YAML_PARSE_DOCUMENT_END_STATE))
            return 0;
        parser->state = YAML_PARSE_BLOCK_NODE_STATE;
        DOCUMENT_START_EVENT_INIT(*event, NULL, NULL, NULL, 1,
                token->start_mark, token->start_mark);
        return 1;
    }

    /* Parse an explicit document. */

    else if (token->type != YAML_STREAM_END_TOKEN)
    {
        yaml_mark_t start_mark, end_mark;
        start_mark = token->start_mark;
        if (!yaml_parser_process_directives(parser, &version_directive,
                    &tag_directives.start, &tag_directives.end))
            return 0;
        token = PEEK_TOKEN(parser);
        if (!token) goto error;
        if (token->type != YAML_DOCUMENT_START_TOKEN) {
            yaml_parser_set_parser_error(parser,
                    "did not find expected <document start>", token->start_mark);
            goto error;
        }
        if (!PUSH(parser, parser->states, YAML_PARSE_DOCUMENT_END_STATE))
            goto error;
        parser->state = YAML_PARSE_DOCUMENT_CONTENT_STATE;
        end_mark = token->end_mark;
        DOCUMENT_START_EVENT_INIT(*event, version_directive,
                tag_directives.start, tag_directives.end, 0,
                start_mark, end_mark);
        SKIP_TOKEN(parser);
        version_directive = NULL;
        tag_directives.start = tag_directives.end = NULL;
        return 1;
    }

    /* Parse the stream end. */

    else
    {
        parser->state = YAML_PARSE_END_STATE;
        STREAM_END_EVENT_INIT(*event, token->start_mark, token->end_mark);
        SKIP_TOKEN(parser);
        return 1;
    }

error:
    yaml_free(version_directive);
    while (tag_directives.start != tag_directives.end) {
        yaml_free(tag_directives.end[-1].handle);
        yaml_free(tag_directives.end[-1].prefix);
        tag_directives.end --;
    }
    yaml_free(tag_directives.start);
    return 0;
}

/*
 * Parse the productions:
 * explicit_document    ::= DIRECTIVE* DOCUMENT-START block_node? DOCUMENT-END*
 *                                                    ***********
 */

static int
yaml_parser_parse_document_content(yaml_parser_t *parser, yaml_event_t *event)
{
    yaml_token_t *token;

    token = PEEK_TOKEN(parser);
    if (!token) return 0;

    if (token->type == YAML_VERSION_DIRECTIVE_TOKEN ||
            token->type == YAML_TAG_DIRECTIVE_TOKEN ||
            token->type == YAML_DOCUMENT_START_TOKEN ||
            token->type == YAML_DOCUMENT_END_TOKEN ||
            token->type == YAML_STREAM_END_TOKEN) {
        parser->state = POP(parser, parser->states);
        return yaml_parser_process_empty_scalar(parser, event,
                token->start_mark);
    }
    else {
        return yaml_parser_parse_node(parser, event, 1, 0);
    }
}

/*
 * Parse the productions:
 * implicit_document    ::= block_node DOCUMENT-END*
 *                                     *************
 * explicit_document    ::= DIRECTIVE* DOCUMENT-START block_node? DOCUMENT-END*
 *                                                                *************
 */

static int
yaml_parser_parse_document_end(yaml_parser_t *parser, yaml_event_t *event)
{
    yaml_token_t *token;
    yaml_mark_t start_mark, end_mark;
    int implicit = 1;

    token = PEEK_TOKEN(parser);
    if (!token) return 0;

    start_mark = end_mark = token->start_mark;

    if (token->type == YAML_DOCUMENT_END_TOKEN) {
        end_mark = token->end_mark;
        SKIP_TOKEN(parser);
        implicit = 0;
    }

    while (!STACK_EMPTY(parser, parser->tag_directives)) {
        yaml_tag_directive_t tag_directive = POP(parser, parser->tag_directives);
        yaml_free(tag_directive.handle);
        yaml_free(tag_directive.prefix);
    }

    parser->state = YAML_PARSE_DOCUMENT_START_STATE;
    DOCUMENT_END_EVENT_INIT(*event, implicit, start_mark, end_mark);

    return 1;
}

/*
 * Parse the productions:
 * block_node_or_indentless_sequence    ::=
 *                          ALIAS
 *                          *****
 *                          | properties (block_content | indentless_block_sequence)?
 *                            **********  *
 *                          | block_content | indentless_block_sequence
 *                            *
 * block_node           ::= ALIAS
 *                          *****
 *                          | properties block_content?
 *                            ********** *
 *                          | block_content
 *                            *
 * flow_node            ::= ALIAS
 *                          *****
 *                          | properties flow_content?
 *                            ********** *
 *                          | flow_content
 *                            *
 * properties           ::= TAG ANCHOR? | ANCHOR TAG?
 *                          *************************
 * block_content        ::= block_collection | flow_collection | SCALAR
 *                                                               ******
 * flow_content         ::= flow_collection | SCALAR
 *                                            ******
 */

static int
yaml_parser_parse_node(yaml_parser_t *parser, yaml_event_t *event,
        int block, int indentless_sequence)
{
    yaml_token_t *token;
    yaml_char_t *anchor = NULL;
    yaml_char_t *tag_handle = NULL;
    yaml_char_t *tag_suffix = NULL;
    yaml_char_t *tag = NULL;
    yaml_mark_t start_mark, end_mark, tag_mark;
    int implicit;

    token = PEEK_TOKEN(parser);
    if (!token) return 0;

    if (token->type == YAML_ALIAS_TOKEN)
    {
        parser->state = POP(parser, parser->states);
        ALIAS_EVENT_INIT(*event, token->data.alias.value,
                token->start_mark, token->end_mark);
        SKIP_TOKEN(parser);
        return 1;
    }

    else
    {
        start_mark = end_mark = token->start_mark;

        if (token->type == YAML_ANCHOR_TOKEN)
        {
            anchor = token->data.anchor.value;
            start_mark = token->start_mark;
            end_mark = token->end_mark;
            SKIP_TOKEN(parser);
            token = PEEK_TOKEN(parser);
            if (!token) goto error;
            if (token->type == YAML_TAG_TOKEN)
            {
                tag_handle = token->data.tag.handle;
                tag_suffix = token->data.tag.suffix;
                tag_mark = token->start_mark;
                end_mark = token->end_mark;
                SKIP_TOKEN(parser);
                token = PEEK_TOKEN(parser);
                if (!token) goto error;
            }
        }
        else if (token->type == YAML_TAG_TOKEN)
        {
            tag_handle = token->data.tag.handle;
            tag_suffix = token->data.tag.suffix;
            start_mark = tag_mark = token->start_mark;
            end_mark = token->end_mark;
            SKIP_TOKEN(parser);
            token = PEEK_TOKEN(parser);
            if (!token) goto error;
            if (token->type == YAML_ANCHOR_TOKEN)
            {
                anchor = token->data.anchor.value;
                end_mark = token->end_mark;
                SKIP_TOKEN(parser);
                token = PEEK_TOKEN(parser);
                if (!token) goto error;
            }
        }

        if (tag_handle) {
            if (!*tag_handle) {
                tag = tag_suffix;
                yaml_free(tag_handle);
                tag_handle = tag_suffix = NULL;
            }
            else {
                yaml_tag_directive_t *tag_directive;
                for (tag_directive = parser->tag_directives.start;
                        tag_directive != parser->tag_directives.top;
                        tag_directive ++) {
                    if (strcmp((char *)tag_directive->handle, (char *)tag_handle) == 0) {
                        size_t prefix_len = strlen((char *)tag_directive->prefix);
                        size_t suffix_len = strlen((char *)tag_suffix);
                        tag = YAML_MALLOC(prefix_len+suffix_len+1);
                        if (!tag) {
                            parser->error = YAML_MEMORY_ERROR;
                            goto error;
                        }
                        memcpy(tag, tag_directive->prefix, prefix_len);
                        memcpy(tag+prefix_len, tag_suffix, suffix_len);
                        tag[prefix_len+suffix_len] = '\0';
                        yaml_free(tag_handle);
                        yaml_free(tag_suffix);
                        tag_handle = tag_suffix = NULL;
                        break;
                    }
                }
                if (!tag) {
                    yaml_parser_set_parser_error_context(parser,
                            "while parsing a node", start_mark,
                            "found undefined tag handle", tag_mark);
                    goto error;
                }
            }
        }

        implicit = (!tag || !*tag);
        if (indentless_sequence && token->type == YAML_BLOCK_ENTRY_TOKEN) {
            end_mark = token->end_mark;
            parser->state = YAML_PARSE_INDENTLESS_SEQUENCE_ENTRY_STATE;
            SEQUENCE_START_EVENT_INIT(*event, anchor, tag, implicit,
                    YAML_BLOCK_SEQUENCE_STYLE, start_mark, end_mark);
            return 1;
        }
        else {
            if (token->type == YAML_SCALAR_TOKEN) {
                int plain_implicit = 0;
                int quoted_implicit = 0;
                end_mark = token->end_mark;
                if ((token->data.scalar.style == YAML_PLAIN_SCALAR_STYLE && !tag)
                        || (tag && strcmp((char *)tag, "!") == 0)) {
                    plain_implicit = 1;
                }
                else if (!tag) {
                    quoted_implicit = 1;
                }
                parser->state = POP(parser, parser->states);
                SCALAR_EVENT_INIT(*event, anchor, tag,
                        token->data.scalar.value, token->data.scalar.length,
                        plain_implicit, quoted_implicit,
                        token->data.scalar.style, start_mark, end_mark);
                SKIP_TOKEN(parser);
                return 1;
            }
            else if (token->type == YAML_FLOW_SEQUENCE_START_TOKEN) {
                end_mark = token->end_mark;
                parser->state = YAML_PARSE_FLOW_SEQUENCE_FIRST_ENTRY_STATE;
                SEQUENCE_START_EVENT_INIT(*event, anchor, tag, implicit,
                        YAML_FLOW_SEQUENCE_STYLE, start_mark, end_mark);
                return 1;
            }
            else if (token->type == YAML_FLOW_MAPPING_START_TOKEN) {
                end_mark = token->end_mark;
                parser->state = YAML_PARSE_FLOW_MAPPING_FIRST_KEY_STATE;
                MAPPING_START_EVENT_INIT(*event, anchor, tag, implicit,
                        YAML_FLOW_MAPPING_STYLE, start_mark, end_mark);
                return 1;
            }
            else if (block && token->type == YAML_BLOCK_SEQUENCE_START_TOKEN) {
                end_mark = token->end_mark;
                parser->state = YAML_PARSE_BLOCK_SEQUENCE_FIRST_ENTRY_STATE;
                SEQUENCE_START_EVENT_INIT(*event, anchor, tag, implicit,
                        YAML_BLOCK_SEQUENCE_STYLE, start_mark, end_mark);
                return 1;
            }
            else if (block && token->type == YAML_BLOCK_MAPPING_START_TOKEN) {
                end_mark = token->end_mark;
                parser->state = YAML_PARSE_BLOCK_MAPPING_FIRST_KEY_STATE;
                MAPPING_START_EVENT_INIT(*event, anchor, tag, implicit,
                        YAML_BLOCK_MAPPING_STYLE, start_mark, end_mark);
                return 1;
            }
            else if (anchor || tag) {
                yaml_char_t *value = YAML_MALLOC(1);
                if (!value) {
                    parser->error = YAML_MEMORY_ERROR;
                    goto error;
                }
                value[0] = '\0';
                parser->state = POP(parser, parser->states);
                SCALAR_EVENT_INIT(*event, anchor, tag, value, 0,
                        implicit, 0, YAML_PLAIN_SCALAR_STYLE,
                        start_mark, end_mark);
                return 1;
            }
            else {
                yaml_parser_set_parser_error_context(parser,
                        (block ? "while parsing a block node"
                         : "while parsing a flow node"), start_mark,
                        "did not find expected node content", token->start_mark);
                goto error;
            }
        }
    }

error:
    yaml_free(anchor);
    yaml_free(tag_handle);
    yaml_free(tag_suffix);
    yaml_free(tag);

    return 0;
}

/*
 * Parse the productions:
 * block_sequence ::= BLOCK-SEQUENCE-START (BLOCK-ENTRY block_node?)* BLOCK-END
 *                    ********************  *********** *             *********
 */

static int
yaml_parser_parse_block_sequence_entry(yaml_parser_t *parser,
        yaml_event_t *event, int first)
{
    yaml_token_t *token;

    if (first) {
        token = PEEK_TOKEN(parser);
        if (!PUSH(parser, parser->marks, token->start_mark))
            return 0;
        SKIP_TOKEN(parser);
    }

    token = PEEK_TOKEN(parser);
    if (!token) return 0;

    if (token->type == YAML_BLOCK_ENTRY_TOKEN)
    {
        yaml_mark_t mark = token->end_mark;
        SKIP_TOKEN(parser);
        token = PEEK_TOKEN(parser);
        if (!token) return 0;
        if (token->type != YAML_BLOCK_ENTRY_TOKEN &&
                token->type != YAML_BLOCK_END_TOKEN) {
            if (!PUSH(parser, parser->states,
                        YAML_PARSE_BLOCK_SEQUENCE_ENTRY_STATE))
                return 0;
            return yaml_parser_parse_node(parser, event, 1, 0);
        }
        else {
            parser->state = YAML_PARSE_BLOCK_SEQUENCE_ENTRY_STATE;
            return yaml_parser_process_empty_scalar(parser, event, mark);
        }
    }

    else if (token->type == YAML_BLOCK_END_TOKEN)
    {
        parser->state = POP(parser, parser->states);
        (void)POP(parser, parser->marks);
        SEQUENCE_END_EVENT_INIT(*event, token->start_mark, token->end_mark);
        SKIP_TOKEN(parser);
        return 1;
    }

    else
    {
        return yaml_parser_set_parser_error_context(parser,
                "while parsing a block collection", POP(parser, parser->marks),
                "did not find expected '-' indicator", token->start_mark);
    }
}

/*
 * Parse the productions:
 * indentless_sequence  ::= (BLOCK-ENTRY block_node?)+
 *                           *********** *
 */

static int
yaml_parser_parse_indentless_sequence_entry(yaml_parser_t *parser,
        yaml_event_t *event)
{
    yaml_token_t *token;

    token = PEEK_TOKEN(parser);
    if (!token) return 0;

    if (token->type == YAML_BLOCK_ENTRY_TOKEN)
    {
        yaml_mark_t mark = token->end_mark;
        SKIP_TOKEN(parser);
        token = PEEK_TOKEN(parser);
        if (!token) return 0;
        if (token->type != YAML_BLOCK_ENTRY_TOKEN &&
                token->type != YAML_KEY_TOKEN &&
                token->type != YAML_VALUE_TOKEN &&
                token->type != YAML_BLOCK_END_TOKEN) {
            if (!PUSH(parser, parser->states,
                        YAML_PARSE_INDENTLESS_SEQUENCE_ENTRY_STATE))
                return 0;
            return yaml_parser_parse_node(parser, event, 1, 0);
        }
        else {
            parser->state = YAML_PARSE_INDENTLESS_SEQUENCE_ENTRY_STATE;
            return yaml_parser_process_empty_scalar(parser, event, mark);
        }
    }

    else
    {
        parser->state = POP(parser, parser->states);
        SEQUENCE_END_EVENT_INIT(*event, token->start_mark, token->start_mark);
        return 1;
    }
}

/*
 * Parse the productions:
 * block_mapping        ::= BLOCK-MAPPING_START
 *                          *******************
 *                          ((KEY block_node_or_indentless_sequence?)?
 *                            *** *
 *                          (VALUE block_node_or_indentless_sequence?)?)*
 *
 *                          BLOCK-END
 *                          *********
 */

static int
yaml_parser_parse_block_mapping_key(yaml_parser_t *parser,
        yaml_event_t *event, int first)
{
    yaml_token_t *token;

    if (first) {
        token = PEEK_TOKEN(parser);
        if (!PUSH(parser, parser->marks, token->start_mark))
            return 0;
        SKIP_TOKEN(parser);
    }

    token = PEEK_TOKEN(parser);
    if (!token) return 0;

    if (token->type == YAML_KEY_TOKEN)
    {
        yaml_mark_t mark = token->end_mark;
        SKIP_TOKEN(parser);
        token = PEEK_TOKEN(parser);
        if (!token) return 0;
        if (token->type != YAML_KEY_TOKEN &&
                token->type != YAML_VALUE_TOKEN &&
                token->type != YAML_BLOCK_END_TOKEN) {
            if (!PUSH(parser, parser->states,
                        YAML_PARSE_BLOCK_MAPPING_VALUE_STATE))
                return 0;
            return yaml_parser_parse_node(parser, event, 1, 1);
        }
        else {
            parser->state = YAML_PARSE_BLOCK_MAPPING_VALUE_STATE;
            return yaml_parser_process_empty_scalar(parser, event, mark);
        }
    }

    else if (token->type == YAML_BLOCK_END_TOKEN)
    {
        parser->state = POP(parser, parser->states);
        (void)POP(parser, parser->marks);
        MAPPING_END_EVENT_INIT(*event, token->start_mark, token->end_mark);
        SKIP_TOKEN(parser);
        return 1;
    }

    else
    {
        return yaml_parser_set_parser_error_context(parser,
                "while parsing a block mapping", POP(parser, parser->marks),
                "did not find expected key", token->start_mark);
    }
}

/*
 * Parse the productions:
 * block_mapping        ::= BLOCK-MAPPING_START
 *
 *                          ((KEY block_node_or_indentless_sequence?)?
 *
 *                          (VALUE block_node_or_indentless_sequence?)?)*
 *                           ***** *
 *                          BLOCK-END
 *
 */

static int
yaml_parser_parse_block_mapping_value(yaml_parser_t *parser,
        yaml_event_t *event)
{
    yaml_token_t *token;

    token = PEEK_TOKEN(parser);
    if (!token) return 0;

    if (token->type == YAML_VALUE_TOKEN)
    {
        yaml_mark_t mark = token->end_mark;
        SKIP_TOKEN(parser);
        token = PEEK_TOKEN(parser);
        if (!token) return 0;
        if (token->type != YAML_KEY_TOKEN &&
                token->type != YAML_VALUE_TOKEN &&
                token->type != YAML_BLOCK_END_TOKEN) {
            if (!PUSH(parser, parser->states,
                        YAML_PARSE_BLOCK_MAPPING_KEY_STATE))
                return 0;
            return yaml_parser_parse_node(parser, event, 1, 1);
        }
        else {
            parser->state = YAML_PARSE_BLOCK_MAPPING_KEY_STATE;
            return yaml_parser_process_empty_scalar(parser, event, mark);
        }
    }

    else
    {
        parser->state = YAML_PARSE_BLOCK_MAPPING_KEY_STATE;
        return yaml_parser_process_empty_scalar(parser, event, token->start_mark);
    }
}

/*
 * Parse the productions:
 * flow_sequence        ::= FLOW-SEQUENCE-START
 *                          *******************
 *                          (flow_sequence_entry FLOW-ENTRY)*
 *                           *                   **********
 *                          flow_sequence_entry?
 *                          *
 *                          FLOW-SEQUENCE-END
 *                          *****************
 * flow_sequence_entry  ::= flow_node | KEY flow_node? (VALUE flow_node?)?
 *                          *
 */

static int
yaml_parser_parse_flow_sequence_entry(yaml_parser_t *parser,
        yaml_event_t *event, int first)
{
    yaml_token_t *token;

    if (first) {
        token = PEEK_TOKEN(parser);
        if (!PUSH(parser, parser->marks, token->start_mark))
            return 0;
        SKIP_TOKEN(parser);
    }

    token = PEEK_TOKEN(parser);
    if (!token) return 0;

    if (token->type != YAML_FLOW_SEQUENCE_END_TOKEN)
    {
        if (!first) {
            if (token->type == YAML_FLOW_ENTRY_TOKEN) {
                SKIP_TOKEN(parser);
                token = PEEK_TOKEN(parser);
                if (!token) return 0;
            }
            else {
                return yaml_parser_set_parser_error_context(parser,
                        "while parsing a flow sequence", POP(parser, parser->marks),
                        "did not find expected ',' or ']'", token->start_mark);
            }
        }

        if (token->type == YAML_KEY_TOKEN) {
            parser->state = YAML_PARSE_FLOW_SEQUENCE_ENTRY_MAPPING_KEY_STATE;
            MAPPING_START_EVENT_INIT(*event, NULL, NULL,
                    1, YAML_FLOW_MAPPING_STYLE,
                    token->start_mark, token->end_mark);
            SKIP_TOKEN(parser);
            return 1;
        }

        else if (token->type != YAML_FLOW_SEQUENCE_END_TOKEN) {
            if (!PUSH(parser, parser->states,
                        YAML_PARSE_FLOW_SEQUENCE_ENTRY_STATE))
                return 0;
            return yaml_parser_parse_node(parser, event, 0, 0);
        }
    }

    parser->state = POP(parser, parser->states);
    (void)POP(parser, parser->marks);
    SEQUENCE_END_EVENT_INIT(*event, token->start_mark, token->end_mark);
    SKIP_TOKEN(parser);
    return 1;
}

/*
 * Parse the productions:
 * flow_sequence_entry  ::= flow_node | KEY flow_node? (VALUE flow_node?)?
 *                                      *** *
 */

static int
yaml_parser_parse_flow_sequence_entry_mapping_key(yaml_parser_t *parser,
        yaml_event_t *event)
{
    yaml_token_t *token;

    token = PEEK_TOKEN(parser);
    if (!token) return 0;

    if (token->type != YAML_VALUE_TOKEN && token->type != YAML_FLOW_ENTRY_TOKEN
            && token->type != YAML_FLOW_SEQUENCE_END_TOKEN) {
        if (!PUSH(parser, parser->states,
                    YAML_PARSE_FLOW_SEQUENCE_ENTRY_MAPPING_VALUE_STATE))
            return 0;
        return yaml_parser_parse_node(parser, event, 0, 0);
    }
    else {
        yaml_mark_t mark = token->end_mark;
        SKIP_TOKEN(parser);
        parser->state = YAML_PARSE_FLOW_SEQUENCE_ENTRY_MAPPING_VALUE_STATE;
        return yaml_parser_process_empty_scalar(parser, event, mark);
    }
}

/*
 * Parse the productions:
 * flow_sequence_entry  ::= flow_node | KEY flow_node? (VALUE flow_node?)?
 *                                                      ***** *
 */

static int
yaml_parser_parse_flow_sequence_entry_mapping_value(yaml_parser_t *parser,
        yaml_event_t *event)
{
    yaml_token_t *token;

    token = PEEK_TOKEN(parser);
    if (!token) return 0;

    if (token->type == YAML_VALUE_TOKEN) {
        SKIP_TOKEN(parser);
        token = PEEK_TOKEN(parser);
        if (!token) return 0;
        if (token->type != YAML_FLOW_ENTRY_TOKEN
                && token->type != YAML_FLOW_SEQUENCE_END_TOKEN) {
            if (!PUSH(parser, parser->states,
                        YAML_PARSE_FLOW_SEQUENCE_ENTRY_MAPPING_END_STATE))
                return 0;
            return yaml_parser_parse_node(parser, event, 0, 0);
        }
    }
    parser->state = YAML_PARSE_FLOW_SEQUENCE_ENTRY_MAPPING_END_STATE;
    return yaml_parser_process_empty_scalar(parser, event, token->start_mark);
}

/*
 * Parse the productions:
 * flow_sequence_entry  ::= flow_node | KEY flow_node? (VALUE flow_node?)?
 *                                                                      *
 */

static int
yaml_parser_parse_flow_sequence_entry_mapping_end(yaml_parser_t *parser,
        yaml_event_t *event)
{
    yaml_token_t *token;

    token = PEEK_TOKEN(parser);
    if (!token) return 0;

    parser->state = YAML_PARSE_FLOW_SEQUENCE_ENTRY_STATE;

    MAPPING_END_EVENT_INIT(*event, token->start_mark, token->start_mark);
    return 1;
}

/*
 * Parse the productions:
 * flow_mapping         ::= FLOW-MAPPING-START
 *                          ******************
 *                          (flow_mapping_entry FLOW-ENTRY)*
 *                           *                  **********
 *                          flow_mapping_entry?
 *                          ******************
 *                          FLOW-MAPPING-END
 *                          ****************
 * flow_mapping_entry   ::= flow_node | KEY flow_node? (VALUE flow_node?)?
 *                          *           *** *
 */

static int
yaml_parser_parse_flow_mapping_key(yaml_parser_t *parser,
        yaml_event_t *event, int first)
{
    yaml_token_t *token;

    if (first) {
        token = PEEK_TOKEN(parser);
        if (!PUSH(parser, parser->marks, token->start_mark))
            return 0;
        SKIP_TOKEN(parser);
    }

    token = PEEK_TOKEN(parser);
    if (!token) return 0;

    if (token->type != YAML_FLOW_MAPPING_END_TOKEN)
    {
        if (!first) {
            if (token->type == YAML_FLOW_ENTRY_TOKEN) {
                SKIP_TOKEN(parser);
                token = PEEK_TOKEN(parser);
                if (!token) return 0;
            }
            else {
                return yaml_parser_set_parser_error_context(parser,
                        "while parsing a flow mapping", POP(parser, parser->marks),
                        "did not find expected ',' or '}'", token->start_mark);
            }
        }

        if (token->type == YAML_KEY_TOKEN) {
            SKIP_TOKEN(parser);
            token = PEEK_TOKEN(parser);
            if (!token) return 0;
            if (token->type != YAML_VALUE_TOKEN
                    && token->type != YAML_FLOW_ENTRY_TOKEN
                    && token->type != YAML_FLOW_MAPPING_END_TOKEN) {
                if (!PUSH(parser, parser->states,
                            YAML_PARSE_FLOW_MAPPING_VALUE_STATE))
                    return 0;
                return yaml_parser_parse_node(parser, event, 0, 0);
            }
            else {
                parser->state = YAML_PARSE_FLOW_MAPPING_VALUE_STATE;
                return yaml_parser_process_empty_scalar(parser, event,
                        token->start_mark);
            }
        }
        else if (token->type != YAML_FLOW_MAPPING_END_TOKEN) {
            if (!PUSH(parser, parser->states,
                        YAML_PARSE_FLOW_MAPPING_EMPTY_VALUE_STATE))
                return 0;
            return yaml_parser_parse_node(parser, event, 0, 0);
        }
    }

    parser->state = POP(parser, parser->states);
    (void)POP(parser, parser->marks);
    MAPPING_END_EVENT_INIT(*event, token->start_mark, token->end_mark);
    SKIP_TOKEN(parser);
    return 1;
}

/*
 * Parse the productions:
 * flow_mapping_entry   ::= flow_node | KEY flow_node? (VALUE flow_node?)?
 *                                   *                  ***** *
 */

static int
yaml_parser_parse_flow_mapping_value(yaml_parser_t *parser,
        yaml_event_t *event, int empty)
{
    yaml_token_t *token;

    token = PEEK_TOKEN(parser);
    if (!token) return 0;

    if (empty) {
        parser->state = YAML_PARSE_FLOW_MAPPING_KEY_STATE;
        return yaml_parser_process_empty_scalar(parser, event,
                token->start_mark);
    }

    if (token->type == YAML_VALUE_TOKEN) {
        SKIP_TOKEN(parser);
        token = PEEK_TOKEN(parser);
        if (!token) return 0;
        if (token->type != YAML_FLOW_ENTRY_TOKEN
                && token->type != YAML_FLOW_MAPPING_END_TOKEN) {
            if (!PUSH(parser, parser->states,
                        YAML_PARSE_FLOW_MAPPING_KEY_STATE))
                return 0;
            return yaml_parser_parse_node(parser, event, 0, 0);
        }
    }

    parser->state = YAML_PARSE_FLOW_MAPPING_KEY_STATE;
    return yaml_parser_process_empty_scalar(parser, event, token->start_mark);
}

/*
 * Generate an empty scalar event.
 */

static int
yaml_parser_process_empty_scalar(yaml_parser_t *parser, yaml_event_t *event,
        yaml_mark_t mark)
{
    yaml_char_t *value;

    value = YAML_MALLOC(1);
    if (!value) {
        parser->error = YAML_MEMORY_ERROR;
        return 0;
    }
    value[0] = '\0';

    SCALAR_EVENT_INIT(*event, NULL, NULL, value, 0,
            1, 0, YAML_PLAIN_SCALAR_STYLE, mark, mark);

    return 1;
}

/*
 * Parse directives.
 */

static int
yaml_parser_process_directives(yaml_parser_t *parser,
        yaml_version_directive_t **version_directive_ref,
        yaml_tag_directive_t **tag_directives_start_ref,
        yaml_tag_directive_t **tag_directives_end_ref)
{
    yaml_tag_directive_t default_tag_directives[] = {
        {(yaml_char_t *)"!", (yaml_char_t *)"!"},
        {(yaml_char_t *)"!!", (yaml_char_t *)"tag:yaml.org,2002:"},
        {NULL, NULL}
    };
    yaml_tag_directive_t *default_tag_directive;
    yaml_version_directive_t *version_directive = NULL;
    struct {
        yaml_tag_directive_t *start;
        yaml_tag_directive_t *end;
        yaml_tag_directive_t *top;
    } tag_directives = { NULL, NULL, NULL };
    yaml_token_t *token;

    if (!STACK_INIT(parser, tag_directives, yaml_tag_directive_t*))
        goto error;

    token = PEEK_TOKEN(parser);
    if (!token) goto error;

    while (token->type == YAML_VERSION_DIRECTIVE_TOKEN ||
            token->type == YAML_TAG_DIRECTIVE_TOKEN)
    {
        if (token->type == YAML_VERSION_DIRECTIVE_TOKEN) {
            if (version_directive) {
                yaml_parser_set_parser_error(parser,
                        "found duplicate %YAML directive", token->start_mark);
                goto error;
            }
            if (token->data.version_directive.major != 1
                    || token->data.version_directive.minor != 1) {
                yaml_parser_set_parser_error(parser,
                        "found incompatible YAML document", token->start_mark);
                goto error;
            }
            version_directive = YAML_MALLOC_STATIC(yaml_version_directive_t);
            if (!version_directive) {
                parser->error = YAML_MEMORY_ERROR;
                goto error;
            }
            version_directive->major = token->data.version_directive.major;
            version_directive->minor = token->data.version_directive.minor;
        }

        else if (token->type == YAML_TAG_DIRECTIVE_TOKEN) {
            yaml_tag_directive_t value;
            value.handle = token->data.tag_directive.handle;
            value.prefix = token->data.tag_directive.prefix;

            if (!yaml_parser_append_tag_directive(parser, value, 0,
                        token->start_mark))
                goto error;
            if (!PUSH(parser, tag_directives, value))
                goto error;
        }

        SKIP_TOKEN(parser);
        token = PEEK_TOKEN(parser);
        if (!token) goto error;
    }

    for (default_tag_directive = default_tag_directives;
            default_tag_directive->handle; default_tag_directive++) {
        if (!yaml_parser_append_tag_directive(parser, *default_tag_directive, 1,
                    token->start_mark))
            goto error;
    }

    if (version_directive_ref) {
        *version_directive_ref = version_directive;
    }
    if (tag_directives_start_ref) {
        if (STACK_EMPTY(parser, tag_directives)) {
            *tag_directives_start_ref = *tag_directives_end_ref = NULL;
            STACK_DEL(parser, tag_directives);
        }
        else {
            *tag_directives_start_ref = tag_directives.start;
            *tag_directives_end_ref = tag_directives.top;
        }
    }
    else {
        STACK_DEL(parser, tag_directives);
    }

    if (!version_directive_ref)
        yaml_free(version_directive);
    return 1;

error:
    yaml_free(version_directive);
    while (!STACK_EMPTY(parser, tag_directives)) {
        yaml_tag_directive_t tag_directive = POP(parser, tag_directives);
        yaml_free(tag_directive.handle);
        yaml_free(tag_directive.prefix);
    }
    STACK_DEL(parser, tag_directives);
    return 0;
}

/*
 * Append a tag directive to the directives stack.
 */

static int
yaml_parser_append_tag_directive(yaml_parser_t *parser,
        yaml_tag_directive_t value, int allow_duplicates, yaml_mark_t mark)
{
    yaml_tag_directive_t *tag_directive;
    yaml_tag_directive_t copy = { NULL, NULL };

    for (tag_directive = parser->tag_directives.start;
            tag_directive != parser->tag_directives.top; tag_directive ++) {
        if (strcmp((char *)value.handle, (char *)tag_directive->handle) == 0) {
            if (allow_duplicates)
                return 1;
            return yaml_parser_set_parser_error(parser,
                    "found duplicate %TAG directive", mark);
        }
    }

    copy.handle = yaml_strdup(value.handle);
    copy.prefix = yaml_strdup(value.prefix);
    if (!copy.handle || !copy.prefix) {
        parser->error = YAML_MEMORY_ERROR;
        goto error;
    }

    if (!PUSH(parser, parser->tag_directives, copy))
        goto error;

    return 1;

error:
    yaml_free(copy.handle);
    yaml_free(copy.prefix);
    return 0;
}


/*
 * Declarations.
 */

static int
yaml_parser_set_reader_error(yaml_parser_t *parser, const char *problem,
        size_t offset, int value);

static int
yaml_parser_update_raw_buffer(yaml_parser_t *parser);

static int
yaml_parser_determine_encoding(yaml_parser_t *parser);

YAML_DECLARE(int)
yaml_parser_update_buffer(yaml_parser_t *parser, size_t length);

/*
 * Set the reader error and return 0.
 */

static int
yaml_parser_set_reader_error(yaml_parser_t *parser, const char *problem,
        size_t offset, int value)
{
    parser->error = YAML_READER_ERROR;
    parser->problem = problem;
    parser->problem_offset = offset;
    parser->problem_value = value;

    return 0;
}

/*
 * Byte order marks.
 */

#define BOM_UTF8    "\xef\xbb\xbf"
#define BOM_UTF16LE "\xff\xfe"
#define BOM_UTF16BE "\xfe\xff"

/*
 * Determine the input stream encoding by checking the BOM symbol. If no BOM is
 * found, the UTF-8 encoding is assumed. Return 1 on success, 0 on failure.
 */

static int
yaml_parser_determine_encoding(yaml_parser_t *parser)
{
    /* Ensure that we had enough bytes in the raw buffer. */

    while (!parser->eof
            && parser->raw_buffer.last - parser->raw_buffer.pointer < 3) {
        if (!yaml_parser_update_raw_buffer(parser)) {
            return 0;
        }
    }

    /* Determine the encoding. */

    if (parser->raw_buffer.last - parser->raw_buffer.pointer >= 2
            && !memcmp(parser->raw_buffer.pointer, BOM_UTF16LE, 2)) {
        parser->encoding = YAML_UTF16LE_ENCODING;
        parser->raw_buffer.pointer += 2;
        parser->offset += 2;
    }
    else if (parser->raw_buffer.last - parser->raw_buffer.pointer >= 2
            && !memcmp(parser->raw_buffer.pointer, BOM_UTF16BE, 2)) {
        parser->encoding = YAML_UTF16BE_ENCODING;
        parser->raw_buffer.pointer += 2;
        parser->offset += 2;
    }
    else if (parser->raw_buffer.last - parser->raw_buffer.pointer >= 3
            && !memcmp(parser->raw_buffer.pointer, BOM_UTF8, 3)) {
        parser->encoding = YAML_UTF8_ENCODING;
        parser->raw_buffer.pointer += 3;
        parser->offset += 3;
    }
    else {
        parser->encoding = YAML_UTF8_ENCODING;
    }

    return 1;
}

/*
 * Update the raw buffer.
 */

static int
yaml_parser_update_raw_buffer(yaml_parser_t *parser)
{
    size_t size_read = 0;

    /* Return if the raw buffer is full. */

    if (parser->raw_buffer.start == parser->raw_buffer.pointer
            && parser->raw_buffer.last == parser->raw_buffer.end)
        return 1;

    /* Return on EOF. */

    if (parser->eof) return 1;

    /* Move the remaining bytes in the raw buffer to the beginning. */

    if (parser->raw_buffer.start < parser->raw_buffer.pointer
            && parser->raw_buffer.pointer < parser->raw_buffer.last) {
        memmove(parser->raw_buffer.start, parser->raw_buffer.pointer,
                parser->raw_buffer.last - parser->raw_buffer.pointer);
    }
    parser->raw_buffer.last -=
        parser->raw_buffer.pointer - parser->raw_buffer.start;
    parser->raw_buffer.pointer = parser->raw_buffer.start;

    /* Call the read handler to fill the buffer. */

    if (!parser->read_handler(parser->read_handler_data, parser->raw_buffer.last,
                parser->raw_buffer.end - parser->raw_buffer.last, &size_read)) {
        return yaml_parser_set_reader_error(parser, "input error",
                parser->offset, -1);
    }
    parser->raw_buffer.last += size_read;
    if (!size_read) {
        parser->eof = 1;
    }

    return 1;
}

/*
 * Ensure that the buffer contains at least `length` characters.
 * Return 1 on success, 0 on failure.
 *
 * The length is supposed to be significantly less that the buffer size.
 */

YAML_DECLARE(int)
yaml_parser_update_buffer(yaml_parser_t *parser, size_t length)
{
    int first = 1;

    assert(parser->read_handler);   /* Read handler must be set. */

    /* If the EOF flag is set and the raw buffer is empty, do nothing. */

    if (parser->eof && parser->raw_buffer.pointer == parser->raw_buffer.last)
        return 1;

    /* Return if the buffer contains enough characters. */

    if (parser->unread >= length)
        return 1;

    /* Determine the input encoding if it is not known yet. */

    if (!parser->encoding) {
        if (!yaml_parser_determine_encoding(parser))
            return 0;
    }

    /* Move the unread characters to the beginning of the buffer. */

    if (parser->buffer.start < parser->buffer.pointer
            && parser->buffer.pointer < parser->buffer.last) {
        size_t size = parser->buffer.last - parser->buffer.pointer;
        memmove(parser->buffer.start, parser->buffer.pointer, size);
        parser->buffer.pointer = parser->buffer.start;
        parser->buffer.last = parser->buffer.start + size;
    }
    else if (parser->buffer.pointer == parser->buffer.last) {
        parser->buffer.pointer = parser->buffer.start;
        parser->buffer.last = parser->buffer.start;
    }

    /* Fill the buffer until it has enough characters. */

    while (parser->unread < length)
    {
        /* Fill the raw buffer if necessary. */

        if (!first || parser->raw_buffer.pointer == parser->raw_buffer.last) {
            if (!yaml_parser_update_raw_buffer(parser)) return 0;
        }
        first = 0;

        /* Decode the raw buffer. */

        while (parser->raw_buffer.pointer != parser->raw_buffer.last)
        {
            unsigned int value = 0, value2 = 0;
            int incomplete = 0;
            unsigned char octet;
            unsigned int width = 0;
            int low, high;
            size_t k;
            size_t raw_unread = parser->raw_buffer.last - parser->raw_buffer.pointer;

            /* Decode the next character. */

            switch (parser->encoding)
            {
                case YAML_UTF8_ENCODING:

                    /*
                     * Decode a UTF-8 character.  Check RFC 3629
                     * (http://www.ietf.org/rfc/rfc3629.txt) for more details.
                     *
                     * The following table (taken from the RFC) is used for
                     * decoding.
                     *
                     *    Char. number range |        UTF-8 octet sequence
                     *      (hexadecimal)    |              (binary)
                     *   --------------------+------------------------------------
                     *   0000 0000-0000 007F | 0xxxxxxx
                     *   0000 0080-0000 07FF | 110xxxxx 10xxxxxx
                     *   0000 0800-0000 FFFF | 1110xxxx 10xxxxxx 10xxxxxx
                     *   0001 0000-0010 FFFF | 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx
                     *
                     * Additionally, the characters in the range 0xD800-0xDFFF
                     * are prohibited as they are reserved for use with UTF-16
                     * surrogate pairs.
                     */

                    /* Determine the length of the UTF-8 sequence. */

                    octet = parser->raw_buffer.pointer[0];
                    width = (octet & 0x80) == 0x00 ? 1 :
                            (octet & 0xE0) == 0xC0 ? 2 :
                            (octet & 0xF0) == 0xE0 ? 3 :
                            (octet & 0xF8) == 0xF0 ? 4 : 0;

                    /* Check if the leading octet is valid. */

                    if (!width)
                        return yaml_parser_set_reader_error(parser,
                                "invalid leading UTF-8 octet",
                                parser->offset, octet);

                    /* Check if the raw buffer contains an incomplete character. */

                    if (width > raw_unread) {
                        if (parser->eof) {
                            return yaml_parser_set_reader_error(parser,
                                    "incomplete UTF-8 octet sequence",
                                    parser->offset, -1);
                        }
                        incomplete = 1;
                        break;
                    }

                    /* Decode the leading octet. */

                    value = (octet & 0x80) == 0x00 ? octet & 0x7F :
                            (octet & 0xE0) == 0xC0 ? octet & 0x1F :
                            (octet & 0xF0) == 0xE0 ? octet & 0x0F :
                            (octet & 0xF8) == 0xF0 ? octet & 0x07 : 0;

                    /* Check and decode the trailing octets. */

                    for (k = 1; k < width; k ++)
                    {
                        octet = parser->raw_buffer.pointer[k];

                        /* Check if the octet is valid. */

                        if ((octet & 0xC0) != 0x80)
                            return yaml_parser_set_reader_error(parser,
                                    "invalid trailing UTF-8 octet",
                                    parser->offset+k, octet);

                        /* Decode the octet. */

                        value = (value << 6) + (octet & 0x3F);
                    }

                    /* Check the length of the sequence against the value. */

                    if (!((width == 1) ||
                            (width == 2 && value >= 0x80) ||
                            (width == 3 && value >= 0x800) ||
                            (width == 4 && value >= 0x10000)))
                        return yaml_parser_set_reader_error(parser,
                                "invalid length of a UTF-8 sequence",
                                parser->offset, -1);

                    /* Check the range of the value. */

                    if ((value >= 0xD800 && value <= 0xDFFF) || value > 0x10FFFF)
                        return yaml_parser_set_reader_error(parser,
                                "invalid Unicode character",
                                parser->offset, value);

                    break;

                case YAML_UTF16LE_ENCODING:
                case YAML_UTF16BE_ENCODING:

                    low = (parser->encoding == YAML_UTF16LE_ENCODING ? 0 : 1);
                    high = (parser->encoding == YAML_UTF16LE_ENCODING ? 1 : 0);

                    /*
                     * The UTF-16 encoding is not as simple as one might
                     * naively think.  Check RFC 2781
                     * (http://www.ietf.org/rfc/rfc2781.txt).
                     *
                     * Normally, two subsequent bytes describe a Unicode
                     * character.  However a special technique (called a
                     * surrogate pair) is used for specifying character
                     * values larger than 0xFFFF.
                     *
                     * A surrogate pair consists of two pseudo-characters:
                     *      high surrogate area (0xD800-0xDBFF)
                     *      low surrogate area (0xDC00-0xDFFF)
                     *
                     * The following formulas are used for decoding
                     * and encoding characters using surrogate pairs:
                     *
                     *  U  = U' + 0x10000   (0x01 00 00 <= U <= 0x10 FF FF)
                     *  U' = yyyyyyyyyyxxxxxxxxxx   (0 <= U' <= 0x0F FF FF)
                     *  W1 = 110110yyyyyyyyyy
                     *  W2 = 110111xxxxxxxxxx
                     *
                     * where U is the character value, W1 is the high surrogate
                     * area, W2 is the low surrogate area.
                     */

                    /* Check for incomplete UTF-16 character. */

                    if (raw_unread < 2) {
                        if (parser->eof) {
                            return yaml_parser_set_reader_error(parser,
                                    "incomplete UTF-16 character",
                                    parser->offset, -1);
                        }
                        incomplete = 1;
                        break;
                    }

                    /* Get the character. */

                    value = parser->raw_buffer.pointer[low]
                        + (parser->raw_buffer.pointer[high] << 8);

                    /* Check for unexpected low surrogate area. */

                    if ((value & 0xFC00) == 0xDC00)
                        return yaml_parser_set_reader_error(parser,
                                "unexpected low surrogate area",
                                parser->offset, value);

                    /* Check for a high surrogate area. */

                    if ((value & 0xFC00) == 0xD800) {

                        width = 4;

                        /* Check for incomplete surrogate pair. */

                        if (raw_unread < 4) {
                            if (parser->eof) {
                                return yaml_parser_set_reader_error(parser,
                                        "incomplete UTF-16 surrogate pair",
                                        parser->offset, -1);
                            }
                            incomplete = 1;
                            break;
                        }

                        /* Get the next character. */

                        value2 = parser->raw_buffer.pointer[low+2]
                            + (parser->raw_buffer.pointer[high+2] << 8);

                        /* Check for a low surrogate area. */

                        if ((value2 & 0xFC00) != 0xDC00)
                            return yaml_parser_set_reader_error(parser,
                                    "expected low surrogate area",
                                    parser->offset+2, value2);

                        /* Generate the value of the surrogate pair. */

                        value = 0x10000 + ((value & 0x3FF) << 10) + (value2 & 0x3FF);
                    }

                    else {
                        width = 2;
                    }

                    break;

                default:
                    assert(1);      /* Impossible. */
            }

            /* Check if the raw buffer contains enough bytes to form a character. */

            if (incomplete) break;

            /*
             * Check if the character is in the allowed range:
             *      #x9 | #xA | #xD | [#x20-#x7E]               (8 bit)
             *      | #x85 | [#xA0-#xD7FF] | [#xE000-#xFFFD]    (16 bit)
             *      | [#x10000-#x10FFFF]                        (32 bit)
             */

            if (! (value == 0x09 || value == 0x0A || value == 0x0D
                        || (value >= 0x20 && value <= 0x7E)
                        || (value == 0x85) || (value >= 0xA0 && value <= 0xD7FF)
                        || (value >= 0xE000 && value <= 0xFFFD)
                        || (value >= 0x10000 && value <= 0x10FFFF)))
                return yaml_parser_set_reader_error(parser,
                        "control characters are not allowed",
                        parser->offset, value);

            /* Move the raw pointers. */

            parser->raw_buffer.pointer += width;
            parser->offset += width;

            /* Finally put the character into the buffer. */

            /* 0000 0000-0000 007F -> 0xxxxxxx */
            if (value <= 0x7F) {
                *(parser->buffer.last++) = value;
            }
            /* 0000 0080-0000 07FF -> 110xxxxx 10xxxxxx */
            else if (value <= 0x7FF) {
                *(parser->buffer.last++) = 0xC0 + (value >> 6);
                *(parser->buffer.last++) = 0x80 + (value & 0x3F);
            }
            /* 0000 0800-0000 FFFF -> 1110xxxx 10xxxxxx 10xxxxxx */
            else if (value <= 0xFFFF) {
                *(parser->buffer.last++) = 0xE0 + (value >> 12);
                *(parser->buffer.last++) = 0x80 + ((value >> 6) & 0x3F);
                *(parser->buffer.last++) = 0x80 + (value & 0x3F);
            }
            /* 0001 0000-0010 FFFF -> 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx */
            else {
                *(parser->buffer.last++) = 0xF0 + (value >> 18);
                *(parser->buffer.last++) = 0x80 + ((value >> 12) & 0x3F);
                *(parser->buffer.last++) = 0x80 + ((value >> 6) & 0x3F);
                *(parser->buffer.last++) = 0x80 + (value & 0x3F);
            }

            parser->unread ++;
        }

        /* On EOF, put NUL into the buffer and return. */

        if (parser->eof) {
            *(parser->buffer.last++) = '\0';
            parser->unread ++;
            return 1;
        }

    }

    if (parser->offset >= MAX_FILE_SIZE) {
        return yaml_parser_set_reader_error(parser, "input is too long",
            parser->offset, -1);
    }

    return 1;
}

/*
 * Ensure that the buffer contains the required number of characters.
 * Return 1 on success, 0 on failure (reader error or memory error).
 */

#define CACHE(parser,length)                                                    \
    (parser->unread >= (length)                                                 \
        ? 1                                                                     \
        : yaml_parser_update_buffer(parser, (length)))

/*
 * Advance the buffer pointer.
 */

#define SKIP(parser)                                                            \
     (parser->mark.index ++,                                                    \
      parser->mark.column ++,                                                   \
      parser->unread --,                                                        \
      parser->buffer.pointer += WIDTH(parser->buffer))

#define SKIP_LINE(parser)                                                       \
     (IS_CRLF(parser->buffer) ?                                                 \
      (parser->mark.index += 2,                                                 \
       parser->mark.column = 0,                                                 \
       parser->mark.line ++,                                                    \
       parser->unread -= 2,                                                     \
       parser->buffer.pointer += 2) :                                           \
      IS_BREAK(parser->buffer) ?                                                \
      (parser->mark.index ++,                                                   \
       parser->mark.column = 0,                                                 \
       parser->mark.line ++,                                                    \
       parser->unread --,                                                       \
       parser->buffer.pointer += WIDTH(parser->buffer)) : 0)

/*
 * Copy a character to a string buffer and advance pointers.
 */

#define READ(parser,string)                                                     \
     (STRING_EXTEND(parser,string) ?                                            \
         (COPY(string,parser->buffer),                                          \
          parser->mark.index ++,                                                \
          parser->mark.column ++,                                               \
          parser->unread --,                                                    \
          1) : 0)

/*
 * Copy a line break character to a string buffer and advance pointers.
 */

#define READ_LINE(parser,string)                                                \
    (STRING_EXTEND(parser,string) ?                                             \
    (((CHECK_AT(parser->buffer,'\r',0)                                          \
       && CHECK_AT(parser->buffer,'\n',1)) ?        /* CR LF -> LF */           \
     (*((string).pointer++) = (yaml_char_t) '\n',                               \
      parser->buffer.pointer += 2,                                              \
      parser->mark.index += 2,                                                  \
      parser->mark.column = 0,                                                  \
      parser->mark.line ++,                                                     \
      parser->unread -= 2) :                                                    \
     (CHECK_AT(parser->buffer,'\r',0)                                           \
      || CHECK_AT(parser->buffer,'\n',0)) ?         /* CR|LF -> LF */           \
     (*((string).pointer++) = (yaml_char_t) '\n',                               \
      parser->buffer.pointer ++,                                                \
      parser->mark.index ++,                                                    \
      parser->mark.column = 0,                                                  \
      parser->mark.line ++,                                                     \
      parser->unread --) :                                                      \
     (CHECK_AT(parser->buffer,'\xC2',0)                                         \
      && CHECK_AT(parser->buffer,'\x85',1)) ?       /* NEL -> LF */             \
     (*((string).pointer++) = (yaml_char_t) '\n',                               \
      parser->buffer.pointer += 2,                                              \
      parser->mark.index ++,                                                    \
      parser->mark.column = 0,                                                  \
      parser->mark.line ++,                                                     \
      parser->unread --) :                                                      \
     (CHECK_AT(parser->buffer,'\xE2',0) &&                                      \
      CHECK_AT(parser->buffer,'\x80',1) &&                                      \
      (CHECK_AT(parser->buffer,'\xA8',2) ||                                     \
       CHECK_AT(parser->buffer,'\xA9',2))) ?        /* LS|PS -> LS|PS */        \
     (*((string).pointer++) = *(parser->buffer.pointer++),                      \
      *((string).pointer++) = *(parser->buffer.pointer++),                      \
      *((string).pointer++) = *(parser->buffer.pointer++),                      \
      parser->mark.index ++,                                                    \
      parser->mark.column = 0,                                                  \
      parser->mark.line ++,                                                     \
      parser->unread --) : 0),                                                  \
    1) : 0)

/*
 * Error handling.
 */

static int
yaml_parser_set_scanner_error(yaml_parser_t *parser, const char *context,
        yaml_mark_t context_mark, const char *problem);

/*
 * High-level token API.
 */

YAML_DECLARE(int)
yaml_parser_fetch_more_tokens(yaml_parser_t *parser);

static int
yaml_parser_fetch_next_token(yaml_parser_t *parser);

/*
 * Potential simple keys.
 */

static int
yaml_parser_stale_simple_keys(yaml_parser_t *parser);

static int
yaml_parser_save_simple_key(yaml_parser_t *parser);

static int
yaml_parser_remove_simple_key(yaml_parser_t *parser);

static int
yaml_parser_increase_flow_level(yaml_parser_t *parser);

static int
yaml_parser_decrease_flow_level(yaml_parser_t *parser);

/*
 * Indentation treatment.
 */

static int
yaml_parser_roll_indent(yaml_parser_t *parser, ptrdiff_t column,
        ptrdiff_t number, yaml_token_type_t type, yaml_mark_t mark);

static int
yaml_parser_unroll_indent(yaml_parser_t *parser, ptrdiff_t column);

/*
 * Token fetchers.
 */

static int
yaml_parser_fetch_stream_start(yaml_parser_t *parser);

static int
yaml_parser_fetch_stream_end(yaml_parser_t *parser);

static int
yaml_parser_fetch_directive(yaml_parser_t *parser);

static int
yaml_parser_fetch_document_indicator(yaml_parser_t *parser,
        yaml_token_type_t type);

static int
yaml_parser_fetch_flow_collection_start(yaml_parser_t *parser,
        yaml_token_type_t type);

static int
yaml_parser_fetch_flow_collection_end(yaml_parser_t *parser,
        yaml_token_type_t type);

static int
yaml_parser_fetch_flow_entry(yaml_parser_t *parser);

static int
yaml_parser_fetch_block_entry(yaml_parser_t *parser);

static int
yaml_parser_fetch_key(yaml_parser_t *parser);

static int
yaml_parser_fetch_value(yaml_parser_t *parser);

static int
yaml_parser_fetch_anchor(yaml_parser_t *parser, yaml_token_type_t type);

static int
yaml_parser_fetch_tag(yaml_parser_t *parser);

static int
yaml_parser_fetch_block_scalar(yaml_parser_t *parser, int literal);

static int
yaml_parser_fetch_flow_scalar(yaml_parser_t *parser, int single);

static int
yaml_parser_fetch_plain_scalar(yaml_parser_t *parser);

/*
 * Token scanners.
 */

static int
yaml_parser_scan_to_next_token(yaml_parser_t *parser);

static int
yaml_parser_scan_directive(yaml_parser_t *parser, yaml_token_t *token);

static int
yaml_parser_scan_directive_name(yaml_parser_t *parser,
        yaml_mark_t start_mark, yaml_char_t **name);

static int
yaml_parser_scan_version_directive_value(yaml_parser_t *parser,
        yaml_mark_t start_mark, int *major, int *minor);

static int
yaml_parser_scan_version_directive_number(yaml_parser_t *parser,
        yaml_mark_t start_mark, int *number);

static int
yaml_parser_scan_tag_directive_value(yaml_parser_t *parser,
        yaml_mark_t mark, yaml_char_t **handle, yaml_char_t **prefix);

static int
yaml_parser_scan_anchor(yaml_parser_t *parser, yaml_token_t *token,
        yaml_token_type_t type);

static int
yaml_parser_scan_tag(yaml_parser_t *parser, yaml_token_t *token);

static int
yaml_parser_scan_tag_handle(yaml_parser_t *parser, int directive,
        yaml_mark_t start_mark, yaml_char_t **handle);

static int
yaml_parser_scan_tag_uri(yaml_parser_t *parser, int directive,
        yaml_char_t *head, yaml_mark_t start_mark, yaml_char_t **uri);

static int
yaml_parser_scan_uri_escapes(yaml_parser_t *parser, int directive,
        yaml_mark_t start_mark, yaml_string_t *string);

static int
yaml_parser_scan_block_scalar(yaml_parser_t *parser, yaml_token_t *token,
        int literal);

static int
yaml_parser_scan_block_scalar_breaks(yaml_parser_t *parser,
        int *indent, yaml_string_t *breaks,
        yaml_mark_t start_mark, yaml_mark_t *end_mark);

static int
yaml_parser_scan_flow_scalar(yaml_parser_t *parser, yaml_token_t *token,
        int single);

static int
yaml_parser_scan_plain_scalar(yaml_parser_t *parser, yaml_token_t *token);

/*
 * Set the scanner error and return 0.
 */

static int
yaml_parser_set_scanner_error(yaml_parser_t *parser, const char *context,
        yaml_mark_t context_mark, const char *problem)
{
    parser->error = YAML_SCANNER_ERROR;
    parser->context = context;
    parser->context_mark = context_mark;
    parser->problem = problem;
    parser->problem_mark = parser->mark;

    return 0;
}

/*
 * Ensure that the tokens queue contains at least one token which can be
 * returned to the Parser.
 */

YAML_DECLARE(int)
yaml_parser_fetch_more_tokens(yaml_parser_t *parser)
{
    int need_more_tokens;

    /* While we need more tokens to fetch, do it. */

    while (1)
    {
        /*
         * Check if we really need to fetch more tokens.
         */

        need_more_tokens = 0;

        if (parser->tokens.head == parser->tokens.tail)
        {
            /* Queue is empty. */

            need_more_tokens = 1;
        }
        else
        {
            yaml_simple_key_t *simple_key;

            /* Check if any potential simple key may occupy the head position. */

            if (!yaml_parser_stale_simple_keys(parser))
                return 0;

            for (simple_key = parser->simple_keys.start;
                    simple_key != parser->simple_keys.top; simple_key++) {
                if (simple_key->possible
                        && simple_key->token_number == parser->tokens_parsed) {
                    need_more_tokens = 1;
                    break;
                }
            }
        }

        /* We are finished. */

        if (!need_more_tokens)
            break;

        /* Fetch the next token. */

        if (!yaml_parser_fetch_next_token(parser))
            return 0;
    }

    parser->token_available = 1;

    return 1;
}

/*
 * The dispatcher for token fetchers.
 */

static int
yaml_parser_fetch_next_token(yaml_parser_t *parser)
{
    /* Ensure that the buffer is initialized. */

    if (!CACHE(parser, 1))
        return 0;

    /* Check if we just started scanning.  Fetch STREAM-START then. */

    if (!parser->stream_start_produced)
        return yaml_parser_fetch_stream_start(parser);

    /* Eat whitespaces and comments until we reach the next token. */

    if (!yaml_parser_scan_to_next_token(parser))
        return 0;

    /* Remove obsolete potential simple keys. */

    if (!yaml_parser_stale_simple_keys(parser))
        return 0;

    /* Check the indentation level against the current column. */

    if (!yaml_parser_unroll_indent(parser, parser->mark.column))
        return 0;

    /*
     * Ensure that the buffer contains at least 4 characters.  4 is the length
     * of the longest indicators ('--- ' and '... ').
     */

    if (!CACHE(parser, 4))
        return 0;

    /* Is it the end of the stream? */

    if (IS_Z(parser->buffer))
        return yaml_parser_fetch_stream_end(parser);

    /* Is it a directive? */

    if (parser->mark.column == 0 && CHECK(parser->buffer, '%'))
        return yaml_parser_fetch_directive(parser);

    /* Is it the document start indicator? */

    if (parser->mark.column == 0
            && CHECK_AT(parser->buffer, '-', 0)
            && CHECK_AT(parser->buffer, '-', 1)
            && CHECK_AT(parser->buffer, '-', 2)
            && IS_BLANKZ_AT(parser->buffer, 3))
        return yaml_parser_fetch_document_indicator(parser,
                YAML_DOCUMENT_START_TOKEN);

    /* Is it the document end indicator? */

    if (parser->mark.column == 0
            && CHECK_AT(parser->buffer, '.', 0)
            && CHECK_AT(parser->buffer, '.', 1)
            && CHECK_AT(parser->buffer, '.', 2)
            && IS_BLANKZ_AT(parser->buffer, 3))
        return yaml_parser_fetch_document_indicator(parser,
                YAML_DOCUMENT_END_TOKEN);

    /* Is it the flow sequence start indicator? */

    if (CHECK(parser->buffer, '['))
        return yaml_parser_fetch_flow_collection_start(parser,
                YAML_FLOW_SEQUENCE_START_TOKEN);

    /* Is it the flow mapping start indicator? */

    if (CHECK(parser->buffer, '{'))
        return yaml_parser_fetch_flow_collection_start(parser,
                YAML_FLOW_MAPPING_START_TOKEN);

    /* Is it the flow sequence end indicator? */

    if (CHECK(parser->buffer, ']'))
        return yaml_parser_fetch_flow_collection_end(parser,
                YAML_FLOW_SEQUENCE_END_TOKEN);

    /* Is it the flow mapping end indicator? */

    if (CHECK(parser->buffer, '}'))
        return yaml_parser_fetch_flow_collection_end(parser,
                YAML_FLOW_MAPPING_END_TOKEN);

    /* Is it the flow entry indicator? */

    if (CHECK(parser->buffer, ','))
        return yaml_parser_fetch_flow_entry(parser);

    /* Is it the block entry indicator? */

    if (CHECK(parser->buffer, '-') && IS_BLANKZ_AT(parser->buffer, 1))
        return yaml_parser_fetch_block_entry(parser);

    /* Is it the key indicator? */

    if (CHECK(parser->buffer, '?')
            && (parser->flow_level || IS_BLANKZ_AT(parser->buffer, 1)))
        return yaml_parser_fetch_key(parser);

    /* Is it the value indicator? */

    if (CHECK(parser->buffer, ':')
            && (parser->flow_level || IS_BLANKZ_AT(parser->buffer, 1)))
        return yaml_parser_fetch_value(parser);

    /* Is it an alias? */

    if (CHECK(parser->buffer, '*'))
        return yaml_parser_fetch_anchor(parser, YAML_ALIAS_TOKEN);

    /* Is it an anchor? */

    if (CHECK(parser->buffer, '&'))
        return yaml_parser_fetch_anchor(parser, YAML_ANCHOR_TOKEN);

    /* Is it a tag? */

    if (CHECK(parser->buffer, '!'))
        return yaml_parser_fetch_tag(parser);

    /* Is it a literal scalar? */

    if (CHECK(parser->buffer, '|') && !parser->flow_level)
        return yaml_parser_fetch_block_scalar(parser, 1);

    /* Is it a folded scalar? */

    if (CHECK(parser->buffer, '>') && !parser->flow_level)
        return yaml_parser_fetch_block_scalar(parser, 0);

    /* Is it a single-quoted scalar? */

    if (CHECK(parser->buffer, '\''))
        return yaml_parser_fetch_flow_scalar(parser, 1);

    /* Is it a double-quoted scalar? */

    if (CHECK(parser->buffer, '"'))
        return yaml_parser_fetch_flow_scalar(parser, 0);

    /*
     * Is it a plain scalar?
     *
     * A plain scalar may start with any non-blank characters except
     *
     *      '-', '?', ':', ',', '[', ']', '{', '}',
     *      '#', '&', '*', '!', '|', '>', '\'', '\"',
     *      '%', '@', '`'.
     *
     * In the block context (and, for the '-' indicator, in the flow context
     * too), it may also start with the characters
     *
     *      '-', '?', ':'
     *
     * if it is followed by a non-space character.
     *
     * The last rule is more restrictive than the specification requires.
     */

    if (!(IS_BLANKZ(parser->buffer) || CHECK(parser->buffer, '-')
                || CHECK(parser->buffer, '?') || CHECK(parser->buffer, ':')
                || CHECK(parser->buffer, ',') || CHECK(parser->buffer, '[')
                || CHECK(parser->buffer, ']') || CHECK(parser->buffer, '{')
                || CHECK(parser->buffer, '}') || CHECK(parser->buffer, '#')
                || CHECK(parser->buffer, '&') || CHECK(parser->buffer, '*')
                || CHECK(parser->buffer, '!') || CHECK(parser->buffer, '|')
                || CHECK(parser->buffer, '>') || CHECK(parser->buffer, '\'')
                || CHECK(parser->buffer, '"') || CHECK(parser->buffer, '%')
                || CHECK(parser->buffer, '@') || CHECK(parser->buffer, '`')) ||
            (CHECK(parser->buffer, '-') && !IS_BLANK_AT(parser->buffer, 1)) ||
            (!parser->flow_level &&
             (CHECK(parser->buffer, '?') || CHECK(parser->buffer, ':'))
             && !IS_BLANKZ_AT(parser->buffer, 1)))
        return yaml_parser_fetch_plain_scalar(parser);

    /*
     * If we don't determine the token type so far, it is an error.
     */

    return yaml_parser_set_scanner_error(parser,
            "while scanning for the next token", parser->mark,
            "found character that cannot start any token");
}

/*
 * Check the list of potential simple keys and remove the positions that
 * cannot contain simple keys anymore.
 */

static int
yaml_parser_stale_simple_keys(yaml_parser_t *parser)
{
    yaml_simple_key_t *simple_key;

    /* Check for a potential simple key for each flow level. */

    for (simple_key = parser->simple_keys.start;
            simple_key != parser->simple_keys.top; simple_key ++)
    {
        /*
         * The specification requires that a simple key
         *
         *  - is limited to a single line,
         *  - is shorter than 1024 characters.
         */

        if (simple_key->possible
                && (simple_key->mark.line < parser->mark.line
                    || simple_key->mark.index+1024 < parser->mark.index)) {

            /* Check if the potential simple key to be removed is required. */

            if (simple_key->required) {
                return yaml_parser_set_scanner_error(parser,
                        "while scanning a simple key", simple_key->mark,
                        "could not find expected ':'");
            }

            simple_key->possible = 0;
        }
    }

    return 1;
}

/*
 * Check if a simple key may start at the current position and add it if
 * needed.
 */

static int
yaml_parser_save_simple_key(yaml_parser_t *parser)
{
    /*
     * A simple key is required at the current position if the scanner is in
     * the block context and the current column coincides with the indentation
     * level.
     */

    int required = (!parser->flow_level
            && parser->indent == (ptrdiff_t)parser->mark.column);

    /*
     * If the current position may start a simple key, save it.
     */

    if (parser->simple_key_allowed)
    {
        yaml_simple_key_t simple_key;
        simple_key.possible = 1;
        simple_key.required = required;
        simple_key.token_number =
            parser->tokens_parsed + (parser->tokens.tail - parser->tokens.head);
        simple_key.mark = parser->mark;

        if (!yaml_parser_remove_simple_key(parser)) return 0;

        *(parser->simple_keys.top-1) = simple_key;
    }

    return 1;
}

/*
 * Remove a potential simple key at the current flow level.
 */

static int
yaml_parser_remove_simple_key(yaml_parser_t *parser)
{
    yaml_simple_key_t *simple_key = parser->simple_keys.top-1;

    if (simple_key->possible)
    {
        /* If the key is required, it is an error. */

        if (simple_key->required) {
            return yaml_parser_set_scanner_error(parser,
                    "while scanning a simple key", simple_key->mark,
                    "could not find expected ':'");
        }
    }

    /* Remove the key from the stack. */

    simple_key->possible = 0;

    return 1;
}

/*
 * Increase the flow level and resize the simple key list if needed.
 */

static int
yaml_parser_increase_flow_level(yaml_parser_t *parser)
{
    yaml_simple_key_t empty_simple_key = { 0, 0, 0, { 0, 0, 0 } };

    /* Reset the simple key on the next level. */

    if (!PUSH(parser, parser->simple_keys, empty_simple_key))
        return 0;

    /* Increase the flow level. */

    if (parser->flow_level == INT_MAX) {
        parser->error = YAML_MEMORY_ERROR;
        return 0;
    }

    parser->flow_level++;

    return 1;
}

/*
 * Decrease the flow level.
 */

static int
yaml_parser_decrease_flow_level(yaml_parser_t *parser)
{
    if (parser->flow_level) {
        parser->flow_level --;
        (void)POP(parser, parser->simple_keys);
    }

    return 1;
}

/*
 * Push the current indentation level to the stack and set the new level
 * the current column is greater than the indentation level.  In this case,
 * append or insert the specified token into the token queue.
 *
 */

static int
yaml_parser_roll_indent(yaml_parser_t *parser, ptrdiff_t column,
        ptrdiff_t number, yaml_token_type_t type, yaml_mark_t mark)
{
    yaml_token_t token;

    /* In the flow context, do nothing. */

    if (parser->flow_level)
        return 1;

    if (parser->indent < column)
    {
        /*
         * Push the current indentation level to the stack and set the new
         * indentation level.
         */

        if (!PUSH(parser, parser->indents, parser->indent))
            return 0;

        if (column > INT_MAX) {
            parser->error = YAML_MEMORY_ERROR;
            return 0;
        }

        parser->indent = column;

        /* Create a token and insert it into the queue. */

        TOKEN_INIT(token, type, mark, mark);

        if (number == -1) {
            if (!ENQUEUE(parser, parser->tokens, token))
                return 0;
        }
        else {
            if (!QUEUE_INSERT(parser,
                        parser->tokens, number - parser->tokens_parsed, token))
                return 0;
        }
    }

    return 1;
}

/*
 * Pop indentation levels from the indents stack until the current level
 * becomes less or equal to the column.  For each indentation level, append
 * the BLOCK-END token.
 */


static int
yaml_parser_unroll_indent(yaml_parser_t *parser, ptrdiff_t column)
{
    yaml_token_t token;

    /* In the flow context, do nothing. */

    if (parser->flow_level)
        return 1;

    /* Loop through the indentation levels in the stack. */

    while (parser->indent > column)
    {
        /* Create a token and append it to the queue. */

        TOKEN_INIT(token, YAML_BLOCK_END_TOKEN, parser->mark, parser->mark);

        if (!ENQUEUE(parser, parser->tokens, token))
            return 0;

        /* Pop the indentation level. */

        parser->indent = POP(parser, parser->indents);
    }

    return 1;
}

/*
 * Initialize the scanner and produce the STREAM-START token.
 */

static int
yaml_parser_fetch_stream_start(yaml_parser_t *parser)
{
    yaml_simple_key_t simple_key = { 0, 0, 0, { 0, 0, 0 } };
    yaml_token_t token;

    /* Set the initial indentation. */

    parser->indent = -1;

    /* Initialize the simple key stack. */

    if (!PUSH(parser, parser->simple_keys, simple_key))
        return 0;

    /* A simple key is allowed at the beginning of the stream. */

    parser->simple_key_allowed = 1;

    /* We have started. */

    parser->stream_start_produced = 1;

    /* Create the STREAM-START token and append it to the queue. */

    STREAM_START_TOKEN_INIT(token, parser->encoding,
            parser->mark, parser->mark);

    if (!ENQUEUE(parser, parser->tokens, token))
        return 0;

    return 1;
}

/*
 * Produce the STREAM-END token and shut down the scanner.
 */

static int
yaml_parser_fetch_stream_end(yaml_parser_t *parser)
{
    yaml_token_t token;

    /* Force new line. */

    if (parser->mark.column != 0) {
        parser->mark.column = 0;
        parser->mark.line ++;
    }

    /* Reset the indentation level. */

    if (!yaml_parser_unroll_indent(parser, -1))
        return 0;

    /* Reset simple keys. */

    if (!yaml_parser_remove_simple_key(parser))
        return 0;

    parser->simple_key_allowed = 0;

    /* Create the STREAM-END token and append it to the queue. */

    STREAM_END_TOKEN_INIT(token, parser->mark, parser->mark);

    if (!ENQUEUE(parser, parser->tokens, token))
        return 0;

    return 1;
}

/*
 * Produce a VERSION-DIRECTIVE or TAG-DIRECTIVE token.
 */

static int
yaml_parser_fetch_directive(yaml_parser_t *parser)
{
    yaml_token_t token;

    /* Reset the indentation level. */

    if (!yaml_parser_unroll_indent(parser, -1))
        return 0;

    /* Reset simple keys. */

    if (!yaml_parser_remove_simple_key(parser))
        return 0;

    parser->simple_key_allowed = 0;

    /* Create the YAML-DIRECTIVE or TAG-DIRECTIVE token. */

    if (!yaml_parser_scan_directive(parser, &token))
        return 0;

    /* Append the token to the queue. */

    if (!ENQUEUE(parser, parser->tokens, token)) {
        yaml_token_delete(&token);
        return 0;
    }

    return 1;
}

/*
 * Produce the DOCUMENT-START or DOCUMENT-END token.
 */

static int
yaml_parser_fetch_document_indicator(yaml_parser_t *parser,
        yaml_token_type_t type)
{
    yaml_mark_t start_mark, end_mark;
    yaml_token_t token;

    /* Reset the indentation level. */

    if (!yaml_parser_unroll_indent(parser, -1))
        return 0;

    /* Reset simple keys. */

    if (!yaml_parser_remove_simple_key(parser))
        return 0;

    parser->simple_key_allowed = 0;

    /* Consume the token. */

    start_mark = parser->mark;

    SKIP(parser);
    SKIP(parser);
    SKIP(parser);

    end_mark = parser->mark;

    /* Create the DOCUMENT-START or DOCUMENT-END token. */

    TOKEN_INIT(token, type, start_mark, end_mark);

    /* Append the token to the queue. */

    if (!ENQUEUE(parser, parser->tokens, token))
        return 0;

    return 1;
}

/*
 * Produce the FLOW-SEQUENCE-START or FLOW-MAPPING-START token.
 */

static int
yaml_parser_fetch_flow_collection_start(yaml_parser_t *parser,
        yaml_token_type_t type)
{
    yaml_mark_t start_mark, end_mark;
    yaml_token_t token;

    /* The indicators '[' and '{' may start a simple key. */

    if (!yaml_parser_save_simple_key(parser))
        return 0;

    /* Increase the flow level. */

    if (!yaml_parser_increase_flow_level(parser))
        return 0;

    /* A simple key may follow the indicators '[' and '{'. */

    parser->simple_key_allowed = 1;

    /* Consume the token. */

    start_mark = parser->mark;
    SKIP(parser);
    end_mark = parser->mark;

    /* Create the FLOW-SEQUENCE-START of FLOW-MAPPING-START token. */

    TOKEN_INIT(token, type, start_mark, end_mark);

    /* Append the token to the queue. */

    if (!ENQUEUE(parser, parser->tokens, token))
        return 0;

    return 1;
}

/*
 * Produce the FLOW-SEQUENCE-END or FLOW-MAPPING-END token.
 */

static int
yaml_parser_fetch_flow_collection_end(yaml_parser_t *parser,
        yaml_token_type_t type)
{
    yaml_mark_t start_mark, end_mark;
    yaml_token_t token;

    /* Reset any potential simple key on the current flow level. */

    if (!yaml_parser_remove_simple_key(parser))
        return 0;

    /* Decrease the flow level. */

    if (!yaml_parser_decrease_flow_level(parser))
        return 0;

    /* No simple keys after the indicators ']' and '}'. */

    parser->simple_key_allowed = 0;

    /* Consume the token. */

    start_mark = parser->mark;
    SKIP(parser);
    end_mark = parser->mark;

    /* Create the FLOW-SEQUENCE-END of FLOW-MAPPING-END token. */

    TOKEN_INIT(token, type, start_mark, end_mark);

    /* Append the token to the queue. */

    if (!ENQUEUE(parser, parser->tokens, token))
        return 0;

    return 1;
}

/*
 * Produce the FLOW-ENTRY token.
 */

static int
yaml_parser_fetch_flow_entry(yaml_parser_t *parser)
{
    yaml_mark_t start_mark, end_mark;
    yaml_token_t token;

    /* Reset any potential simple keys on the current flow level. */

    if (!yaml_parser_remove_simple_key(parser))
        return 0;

    /* Simple keys are allowed after ','. */

    parser->simple_key_allowed = 1;

    /* Consume the token. */

    start_mark = parser->mark;
    SKIP(parser);
    end_mark = parser->mark;

    /* Create the FLOW-ENTRY token and append it to the queue. */

    TOKEN_INIT(token, YAML_FLOW_ENTRY_TOKEN, start_mark, end_mark);

    if (!ENQUEUE(parser, parser->tokens, token))
        return 0;

    return 1;
}

/*
 * Produce the BLOCK-ENTRY token.
 */

static int
yaml_parser_fetch_block_entry(yaml_parser_t *parser)
{
    yaml_mark_t start_mark, end_mark;
    yaml_token_t token;

    /* Check if the scanner is in the block context. */

    if (!parser->flow_level)
    {
        /* Check if we are allowed to start a new entry. */

        if (!parser->simple_key_allowed) {
            return yaml_parser_set_scanner_error(parser, NULL, parser->mark,
                    "block sequence entries are not allowed in this context");
        }

        /* Add the BLOCK-SEQUENCE-START token if needed. */

        if (!yaml_parser_roll_indent(parser, parser->mark.column, -1,
                    YAML_BLOCK_SEQUENCE_START_TOKEN, parser->mark))
            return 0;
    }
    else
    {
        /*
         * It is an error for the '-' indicator to occur in the flow context,
         * but we let the Parser detect and report about it because the Parser
         * is able to point to the context.
         */
    }

    /* Reset any potential simple keys on the current flow level. */

    if (!yaml_parser_remove_simple_key(parser))
        return 0;

    /* Simple keys are allowed after '-'. */

    parser->simple_key_allowed = 1;

    /* Consume the token. */

    start_mark = parser->mark;
    SKIP(parser);
    end_mark = parser->mark;

    /* Create the BLOCK-ENTRY token and append it to the queue. */

    TOKEN_INIT(token, YAML_BLOCK_ENTRY_TOKEN, start_mark, end_mark);

    if (!ENQUEUE(parser, parser->tokens, token))
        return 0;

    return 1;
}

/*
 * Produce the KEY token.
 */

static int
yaml_parser_fetch_key(yaml_parser_t *parser)
{
    yaml_mark_t start_mark, end_mark;
    yaml_token_t token;

    /* In the block context, additional checks are required. */

    if (!parser->flow_level)
    {
        /* Check if we are allowed to start a new key (not necessary simple). */

        if (!parser->simple_key_allowed) {
            return yaml_parser_set_scanner_error(parser, NULL, parser->mark,
                    "mapping keys are not allowed in this context");
        }

        /* Add the BLOCK-MAPPING-START token if needed. */

        if (!yaml_parser_roll_indent(parser, parser->mark.column, -1,
                    YAML_BLOCK_MAPPING_START_TOKEN, parser->mark))
            return 0;
    }

    /* Reset any potential simple keys on the current flow level. */

    if (!yaml_parser_remove_simple_key(parser))
        return 0;

    /* Simple keys are allowed after '?' in the block context. */

    parser->simple_key_allowed = (!parser->flow_level);

    /* Consume the token. */

    start_mark = parser->mark;
    SKIP(parser);
    end_mark = parser->mark;

    /* Create the KEY token and append it to the queue. */

    TOKEN_INIT(token, YAML_KEY_TOKEN, start_mark, end_mark);

    if (!ENQUEUE(parser, parser->tokens, token))
        return 0;

    return 1;
}

/*
 * Produce the VALUE token.
 */

static int
yaml_parser_fetch_value(yaml_parser_t *parser)
{
    yaml_mark_t start_mark, end_mark;
    yaml_token_t token;
    yaml_simple_key_t *simple_key = parser->simple_keys.top-1;

    /* Have we found a simple key? */

    if (simple_key->possible)
    {

        /* Create the KEY token and insert it into the queue. */

        TOKEN_INIT(token, YAML_KEY_TOKEN, simple_key->mark, simple_key->mark);

        if (!QUEUE_INSERT(parser, parser->tokens,
                    simple_key->token_number - parser->tokens_parsed, token))
            return 0;

        /* In the block context, we may need to add the BLOCK-MAPPING-START token. */

        if (!yaml_parser_roll_indent(parser, simple_key->mark.column,
                    simple_key->token_number,
                    YAML_BLOCK_MAPPING_START_TOKEN, simple_key->mark))
            return 0;

        /* Remove the simple key. */

        simple_key->possible = 0;

        /* A simple key cannot follow another simple key. */

        parser->simple_key_allowed = 0;
    }
    else
    {
        /* The ':' indicator follows a complex key. */

        /* In the block context, extra checks are required. */

        if (!parser->flow_level)
        {
            /* Check if we are allowed to start a complex value. */

            if (!parser->simple_key_allowed) {
                return yaml_parser_set_scanner_error(parser, NULL, parser->mark,
                        "mapping values are not allowed in this context");
            }

            /* Add the BLOCK-MAPPING-START token if needed. */

            if (!yaml_parser_roll_indent(parser, parser->mark.column, -1,
                        YAML_BLOCK_MAPPING_START_TOKEN, parser->mark))
                return 0;
        }

        /* Simple keys after ':' are allowed in the block context. */

        parser->simple_key_allowed = (!parser->flow_level);
    }

    /* Consume the token. */

    start_mark = parser->mark;
    SKIP(parser);
    end_mark = parser->mark;

    /* Create the VALUE token and append it to the queue. */

    TOKEN_INIT(token, YAML_VALUE_TOKEN, start_mark, end_mark);

    if (!ENQUEUE(parser, parser->tokens, token))
        return 0;

    return 1;
}

/*
 * Produce the ALIAS or ANCHOR token.
 */

static int
yaml_parser_fetch_anchor(yaml_parser_t *parser, yaml_token_type_t type)
{
    yaml_token_t token;

    /* An anchor or an alias could be a simple key. */

    if (!yaml_parser_save_simple_key(parser))
        return 0;

    /* A simple key cannot follow an anchor or an alias. */

    parser->simple_key_allowed = 0;

    /* Create the ALIAS or ANCHOR token and append it to the queue. */

    if (!yaml_parser_scan_anchor(parser, &token, type))
        return 0;

    if (!ENQUEUE(parser, parser->tokens, token)) {
        yaml_token_delete(&token);
        return 0;
    }
    return 1;
}

/*
 * Produce the TAG token.
 */

static int
yaml_parser_fetch_tag(yaml_parser_t *parser)
{
    yaml_token_t token;

    /* A tag could be a simple key. */

    if (!yaml_parser_save_simple_key(parser))
        return 0;

    /* A simple key cannot follow a tag. */

    parser->simple_key_allowed = 0;

    /* Create the TAG token and append it to the queue. */

    if (!yaml_parser_scan_tag(parser, &token))
        return 0;

    if (!ENQUEUE(parser, parser->tokens, token)) {
        yaml_token_delete(&token);
        return 0;
    }

    return 1;
}

/*
 * Produce the SCALAR(...,literal) or SCALAR(...,folded) tokens.
 */

static int
yaml_parser_fetch_block_scalar(yaml_parser_t *parser, int literal)
{
    yaml_token_t token;

    /* Remove any potential simple keys. */

    if (!yaml_parser_remove_simple_key(parser))
        return 0;

    /* A simple key may follow a block scalar. */

    parser->simple_key_allowed = 1;

    /* Create the SCALAR token and append it to the queue. */

    if (!yaml_parser_scan_block_scalar(parser, &token, literal))
        return 0;

    if (!ENQUEUE(parser, parser->tokens, token)) {
        yaml_token_delete(&token);
        return 0;
    }

    return 1;
}

/*
 * Produce the SCALAR(...,single-quoted) or SCALAR(...,double-quoted) tokens.
 */

static int
yaml_parser_fetch_flow_scalar(yaml_parser_t *parser, int single)
{
    yaml_token_t token;

    /* A plain scalar could be a simple key. */

    if (!yaml_parser_save_simple_key(parser))
        return 0;

    /* A simple key cannot follow a flow scalar. */

    parser->simple_key_allowed = 0;

    /* Create the SCALAR token and append it to the queue. */

    if (!yaml_parser_scan_flow_scalar(parser, &token, single))
        return 0;

    if (!ENQUEUE(parser, parser->tokens, token)) {
        yaml_token_delete(&token);
        return 0;
    }

    return 1;
}

/*
 * Produce the SCALAR(...,plain) token.
 */

static int
yaml_parser_fetch_plain_scalar(yaml_parser_t *parser)
{
    yaml_token_t token;

    /* A plain scalar could be a simple key. */

    if (!yaml_parser_save_simple_key(parser))
        return 0;

    /* A simple key cannot follow a flow scalar. */

    parser->simple_key_allowed = 0;

    /* Create the SCALAR token and append it to the queue. */

    if (!yaml_parser_scan_plain_scalar(parser, &token))
        return 0;

    if (!ENQUEUE(parser, parser->tokens, token)) {
        yaml_token_delete(&token);
        return 0;
    }

    return 1;
}

/*
 * Eat whitespaces and comments until the next token is found.
 */

static int
yaml_parser_scan_to_next_token(yaml_parser_t *parser)
{
    /* Until the next token is not found. */

    while (1)
    {
        /* Allow the BOM mark to start a line. */

        if (!CACHE(parser, 1)) return 0;

        if (parser->mark.column == 0 && IS_BOM(parser->buffer))
            SKIP(parser);

        /*
         * Eat whitespaces.
         *
         * Tabs are allowed:
         *
         *  - in the flow context;
         *  - in the block context, but not at the beginning of the line or
         *  after '-', '?', or ':' (complex value).
         */

        if (!CACHE(parser, 1)) return 0;

        while (CHECK(parser->buffer,' ') ||
                ((parser->flow_level || !parser->simple_key_allowed) &&
                 CHECK(parser->buffer, '\t'))) {
            SKIP(parser);
            if (!CACHE(parser, 1)) return 0;
        }

        /* Eat a comment until a line break. */

        if (CHECK(parser->buffer, '#')) {
            while (!IS_BREAKZ(parser->buffer)) {
                SKIP(parser);
                if (!CACHE(parser, 1)) return 0;
            }
        }

        /* If it is a line break, eat it. */

        if (IS_BREAK(parser->buffer))
        {
            if (!CACHE(parser, 2)) return 0;
            SKIP_LINE(parser);

            /* In the block context, a new line may start a simple key. */

            if (!parser->flow_level) {
                parser->simple_key_allowed = 1;
            }
        }
        else
        {
            /* We have found a token. */

            break;
        }
    }

    return 1;
}

/*
 * Scan a YAML-DIRECTIVE or TAG-DIRECTIVE token.
 *
 * Scope:
 *      %YAML    1.1    # a comment \n
 *      ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
 *      %TAG    !yaml!  tag:yaml.org,2002:  \n
 *      ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
 */

int
yaml_parser_scan_directive(yaml_parser_t *parser, yaml_token_t *token)
{
    yaml_mark_t start_mark, end_mark;
    yaml_char_t *name = NULL;
    int major, minor;
    yaml_char_t *handle = NULL, *prefix = NULL;

    /* Eat '%'. */

    start_mark = parser->mark;

    SKIP(parser);

    /* Scan the directive name. */

    if (!yaml_parser_scan_directive_name(parser, start_mark, &name))
        goto error;

    /* Is it a YAML directive? */

    if (strcmp((char *)name, "YAML") == 0)
    {
        /* Scan the VERSION directive value. */

        if (!yaml_parser_scan_version_directive_value(parser, start_mark,
                    &major, &minor))
            goto error;

        end_mark = parser->mark;

        /* Create a VERSION-DIRECTIVE token. */

        VERSION_DIRECTIVE_TOKEN_INIT(*token, major, minor,
                start_mark, end_mark);
    }

    /* Is it a TAG directive? */

    else if (strcmp((char *)name, "TAG") == 0)
    {
        /* Scan the TAG directive value. */

        if (!yaml_parser_scan_tag_directive_value(parser, start_mark,
                    &handle, &prefix))
            goto error;

        end_mark = parser->mark;

        /* Create a TAG-DIRECTIVE token. */

        TAG_DIRECTIVE_TOKEN_INIT(*token, handle, prefix,
                start_mark, end_mark);
    }

    /* Unknown directive. */

    else
    {
        yaml_parser_set_scanner_error(parser, "while scanning a directive",
                start_mark, "found unknown directive name");
        goto error;
    }

    /* Eat the rest of the line including any comments. */

    if (!CACHE(parser, 1)) goto error;

    while (IS_BLANK(parser->buffer)) {
        SKIP(parser);
        if (!CACHE(parser, 1)) goto error;
    }

    if (CHECK(parser->buffer, '#')) {
        while (!IS_BREAKZ(parser->buffer)) {
            SKIP(parser);
            if (!CACHE(parser, 1)) goto error;
        }
    }

    /* Check if we are at the end of the line. */

    if (!IS_BREAKZ(parser->buffer)) {
        yaml_parser_set_scanner_error(parser, "while scanning a directive",
                start_mark, "did not find expected comment or line break");
        goto error;
    }

    /* Eat a line break. */

    if (IS_BREAK(parser->buffer)) {
        if (!CACHE(parser, 2)) goto error;
        SKIP_LINE(parser);
    }

    yaml_free(name);

    return 1;

error:
    yaml_free(prefix);
    yaml_free(handle);
    yaml_free(name);
    return 0;
}

/*
 * Scan the directive name.
 *
 * Scope:
 *      %YAML   1.1     # a comment \n
 *       ^^^^
 *      %TAG    !yaml!  tag:yaml.org,2002:  \n
 *       ^^^
 */

static int
yaml_parser_scan_directive_name(yaml_parser_t *parser,
        yaml_mark_t start_mark, yaml_char_t **name)
{
    yaml_string_t string = NULL_STRING;

    if (!STRING_INIT(parser, string, INITIAL_STRING_SIZE)) goto error;

    /* Consume the directive name. */

    if (!CACHE(parser, 1)) goto error;

    while (IS_ALPHA(parser->buffer))
    {
        if (!READ(parser, string)) goto error;
        if (!CACHE(parser, 1)) goto error;
    }

    /* Check if the name is empty. */

    if (string.start == string.pointer) {
        yaml_parser_set_scanner_error(parser, "while scanning a directive",
                start_mark, "could not find expected directive name");
        goto error;
    }

    /* Check for an blank character after the name. */

    if (!IS_BLANKZ(parser->buffer)) {
        yaml_parser_set_scanner_error(parser, "while scanning a directive",
                start_mark, "found unexpected non-alphabetical character");
        goto error;
    }

    *name = string.start;

    return 1;

error:
    STRING_DEL(parser, string);
    return 0;
}

/*
 * Scan the value of VERSION-DIRECTIVE.
 *
 * Scope:
 *      %YAML   1.1     # a comment \n
 *           ^^^^^^
 */

static int
yaml_parser_scan_version_directive_value(yaml_parser_t *parser,
        yaml_mark_t start_mark, int *major, int *minor)
{
    /* Eat whitespaces. */

    if (!CACHE(parser, 1)) return 0;

    while (IS_BLANK(parser->buffer)) {
        SKIP(parser);
        if (!CACHE(parser, 1)) return 0;
    }

    /* Consume the major version number. */

    if (!yaml_parser_scan_version_directive_number(parser, start_mark, major))
        return 0;

    /* Eat '.'. */

    if (!CHECK(parser->buffer, '.')) {
        return yaml_parser_set_scanner_error(parser, "while scanning a %YAML directive",
                start_mark, "did not find expected digit or '.' character");
    }

    SKIP(parser);

    /* Consume the minor version number. */

    if (!yaml_parser_scan_version_directive_number(parser, start_mark, minor))
        return 0;

    return 1;
}

#define MAX_NUMBER_LENGTH   9

/*
 * Scan the version number of VERSION-DIRECTIVE.
 *
 * Scope:
 *      %YAML   1.1     # a comment \n
 *              ^
 *      %YAML   1.1     # a comment \n
 *                ^
 */

static int
yaml_parser_scan_version_directive_number(yaml_parser_t *parser,
        yaml_mark_t start_mark, int *number)
{
    int value = 0;
    size_t length = 0;

    /* Repeat while the next character is digit. */

    if (!CACHE(parser, 1)) return 0;

    while (IS_DIGIT(parser->buffer))
    {
        /* Check if the number is too long. */

        if (++length > MAX_NUMBER_LENGTH) {
            return yaml_parser_set_scanner_error(parser, "while scanning a %YAML directive",
                    start_mark, "found extremely long version number");
        }

        value = value*10 + AS_DIGIT(parser->buffer);

        SKIP(parser);

        if (!CACHE(parser, 1)) return 0;
    }

    /* Check if the number was present. */

    if (!length) {
        return yaml_parser_set_scanner_error(parser, "while scanning a %YAML directive",
                start_mark, "did not find expected version number");
    }

    *number = value;

    return 1;
}

/*
 * Scan the value of a TAG-DIRECTIVE token.
 *
 * Scope:
 *      %TAG    !yaml!  tag:yaml.org,2002:  \n
 *          ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
 */

static int
yaml_parser_scan_tag_directive_value(yaml_parser_t *parser,
        yaml_mark_t start_mark, yaml_char_t **handle, yaml_char_t **prefix)
{
    yaml_char_t *handle_value = NULL;
    yaml_char_t *prefix_value = NULL;

    /* Eat whitespaces. */

    if (!CACHE(parser, 1)) goto error;

    while (IS_BLANK(parser->buffer)) {
        SKIP(parser);
        if (!CACHE(parser, 1)) goto error;
    }

    /* Scan a handle. */

    if (!yaml_parser_scan_tag_handle(parser, 1, start_mark, &handle_value))
        goto error;

    /* Expect a whitespace. */

    if (!CACHE(parser, 1)) goto error;

    if (!IS_BLANK(parser->buffer)) {
        yaml_parser_set_scanner_error(parser, "while scanning a %TAG directive",
                start_mark, "did not find expected whitespace");
        goto error;
    }

    /* Eat whitespaces. */

    while (IS_BLANK(parser->buffer)) {
        SKIP(parser);
        if (!CACHE(parser, 1)) goto error;
    }

    /* Scan a prefix. */

    if (!yaml_parser_scan_tag_uri(parser, 1, NULL, start_mark, &prefix_value))
        goto error;

    /* Expect a whitespace or line break. */

    if (!CACHE(parser, 1)) goto error;

    if (!IS_BLANKZ(parser->buffer)) {
        yaml_parser_set_scanner_error(parser, "while scanning a %TAG directive",
                start_mark, "did not find expected whitespace or line break");
        goto error;
    }

    *handle = handle_value;
    *prefix = prefix_value;

    return 1;

error:
    yaml_free(handle_value);
    yaml_free(prefix_value);
    return 0;
}

static int
yaml_parser_scan_anchor(yaml_parser_t *parser, yaml_token_t *token,
        yaml_token_type_t type)
{
    int length = 0;
    yaml_mark_t start_mark, end_mark;
    yaml_string_t string = NULL_STRING;

    if (!STRING_INIT(parser, string, INITIAL_STRING_SIZE)) goto error;

    /* Eat the indicator character. */

    start_mark = parser->mark;

    SKIP(parser);

    /* Consume the value. */

    if (!CACHE(parser, 1)) goto error;

    while (IS_ALPHA(parser->buffer)) {
        if (!READ(parser, string)) goto error;
        if (!CACHE(parser, 1)) goto error;
        length ++;
    }

    end_mark = parser->mark;

    /*
     * Check if length of the anchor is greater than 0 and it is followed by
     * a whitespace character or one of the indicators:
     *
     *      '?', ':', ',', ']', '}', '%', '@', '`'.
     */

    if (!length || !(IS_BLANKZ(parser->buffer) || CHECK(parser->buffer, '?')
                || CHECK(parser->buffer, ':') || CHECK(parser->buffer, ',')
                || CHECK(parser->buffer, ']') || CHECK(parser->buffer, '}')
                || CHECK(parser->buffer, '%') || CHECK(parser->buffer, '@')
                || CHECK(parser->buffer, '`'))) {
        yaml_parser_set_scanner_error(parser, type == YAML_ANCHOR_TOKEN ?
                "while scanning an anchor" : "while scanning an alias", start_mark,
                "did not find expected alphabetic or numeric character");
        goto error;
    }

    /* Create a token. */

    if (type == YAML_ANCHOR_TOKEN) {
        ANCHOR_TOKEN_INIT(*token, string.start, start_mark, end_mark);
    }
    else {
        ALIAS_TOKEN_INIT(*token, string.start, start_mark, end_mark);
    }

    return 1;

error:
    STRING_DEL(parser, string);
    return 0;
}

/*
 * Scan a TAG token.
 */

static int
yaml_parser_scan_tag(yaml_parser_t *parser, yaml_token_t *token)
{
    yaml_char_t *handle = NULL;
    yaml_char_t *suffix = NULL;
    yaml_mark_t start_mark, end_mark;

    start_mark = parser->mark;

    /* Check if the tag is in the canonical form. */

    if (!CACHE(parser, 2)) goto error;

    if (CHECK_AT(parser->buffer, '<', 1))
    {
        /* Set the handle to '' */

        handle = YAML_MALLOC(1);
        if (!handle) goto error;
        handle[0] = '\0';

        /* Eat '!<' */

        SKIP(parser);
        SKIP(parser);

        /* Consume the tag value. */

        if (!yaml_parser_scan_tag_uri(parser, 0, NULL, start_mark, &suffix))
            goto error;

        /* Check for '>' and eat it. */

        if (!CHECK(parser->buffer, '>')) {
            yaml_parser_set_scanner_error(parser, "while scanning a tag",
                    start_mark, "did not find the expected '>'");
            goto error;
        }

        SKIP(parser);
    }
    else
    {
        /* The tag has either the '!suffix' or the '!handle!suffix' form. */

        /* First, try to scan a handle. */

        if (!yaml_parser_scan_tag_handle(parser, 0, start_mark, &handle))
            goto error;

        /* Check if it is, indeed, handle. */

        if (handle[0] == '!' && handle[1] != '\0' && handle[strlen((char *)handle)-1] == '!')
        {
            /* Scan the suffix now. */

            if (!yaml_parser_scan_tag_uri(parser, 0, NULL, start_mark, &suffix))
                goto error;
        }
        else
        {
            /* It wasn't a handle after all.  Scan the rest of the tag. */

            if (!yaml_parser_scan_tag_uri(parser, 0, handle, start_mark, &suffix))
                goto error;

            /* Set the handle to '!'. */

            yaml_free(handle);
            handle = YAML_MALLOC(2);
            if (!handle) goto error;
            handle[0] = '!';
            handle[1] = '\0';

            /*
             * A special case: the '!' tag.  Set the handle to '' and the
             * suffix to '!'.
             */

            if (suffix[0] == '\0') {
                yaml_char_t *tmp = handle;
                handle = suffix;
                suffix = tmp;
            }
        }
    }

    /* Check the character which ends the tag. */

    if (!CACHE(parser, 1)) goto error;

    if (!IS_BLANKZ(parser->buffer)) {
        yaml_parser_set_scanner_error(parser, "while scanning a tag",
                start_mark, "did not find expected whitespace or line break");
        goto error;
    }

    end_mark = parser->mark;

    /* Create a token. */

    TAG_TOKEN_INIT(*token, handle, suffix, start_mark, end_mark);

    return 1;

error:
    yaml_free(handle);
    yaml_free(suffix);
    return 0;
}

/*
 * Scan a tag handle.
 */

static int
yaml_parser_scan_tag_handle(yaml_parser_t *parser, int directive,
        yaml_mark_t start_mark, yaml_char_t **handle)
{
    yaml_string_t string = NULL_STRING;

    if (!STRING_INIT(parser, string, INITIAL_STRING_SIZE)) goto error;

    /* Check the initial '!' character. */

    if (!CACHE(parser, 1)) goto error;

    if (!CHECK(parser->buffer, '!')) {
        yaml_parser_set_scanner_error(parser, directive ?
                "while scanning a tag directive" : "while scanning a tag",
                start_mark, "did not find expected '!'");
        goto error;
    }

    /* Copy the '!' character. */

    if (!READ(parser, string)) goto error;

    /* Copy all subsequent alphabetical and numerical characters. */

    if (!CACHE(parser, 1)) goto error;

    while (IS_ALPHA(parser->buffer))
    {
        if (!READ(parser, string)) goto error;
        if (!CACHE(parser, 1)) goto error;
    }

    /* Check if the trailing character is '!' and copy it. */

    if (CHECK(parser->buffer, '!'))
    {
        if (!READ(parser, string)) goto error;
    }
    else
    {
        /*
         * It's either the '!' tag or not really a tag handle.  If it's a %TAG
         * directive, it's an error.  If it's a tag token, it must be a part of
         * URI.
         */

        if (directive && !(string.start[0] == '!' && string.start[1] == '\0')) {
            yaml_parser_set_scanner_error(parser, "while parsing a tag directive",
                    start_mark, "did not find expected '!'");
            goto error;
        }
    }

    *handle = string.start;

    return 1;

error:
    STRING_DEL(parser, string);
    return 0;
}

/*
 * Scan a tag.
 */

static int
yaml_parser_scan_tag_uri(yaml_parser_t *parser, int directive,
        yaml_char_t *head, yaml_mark_t start_mark, yaml_char_t **uri)
{
    size_t length = head ? strlen((char *)head) : 0;
    yaml_string_t string = NULL_STRING;

    if (!STRING_INIT(parser, string, INITIAL_STRING_SIZE)) goto error;

    /* Resize the string to include the head. */

    while ((size_t)(string.end - string.start) <= length) {
        if (!yaml_string_extend(&string.start, &string.pointer, &string.end)) {
            parser->error = YAML_MEMORY_ERROR;
            goto error;
        }
    }

    /*
     * Copy the head if needed.
     *
     * Note that we don't copy the leading '!' character.
     */

    if (length > 1) {
        memcpy(string.start, head+1, length-1);
        string.pointer += length-1;
    }

    /* Scan the tag. */

    if (!CACHE(parser, 1)) goto error;

    /*
     * The set of characters that may appear in URI is as follows:
     *
     *      '0'-'9', 'A'-'Z', 'a'-'z', '_', '-', ';', '/', '?', ':', '@', '&',
     *      '=', '+', '$', ',', '.', '!', '~', '*', '\'', '(', ')', '[', ']',
     *      '%'.
     */

    while (IS_ALPHA(parser->buffer) || CHECK(parser->buffer, ';')
            || CHECK(parser->buffer, '/') || CHECK(parser->buffer, '?')
            || CHECK(parser->buffer, ':') || CHECK(parser->buffer, '@')
            || CHECK(parser->buffer, '&') || CHECK(parser->buffer, '=')
            || CHECK(parser->buffer, '+') || CHECK(parser->buffer, '$')
            || CHECK(parser->buffer, ',') || CHECK(parser->buffer, '.')
            || CHECK(parser->buffer, '!') || CHECK(parser->buffer, '~')
            || CHECK(parser->buffer, '*') || CHECK(parser->buffer, '\'')
            || CHECK(parser->buffer, '(') || CHECK(parser->buffer, ')')
            || CHECK(parser->buffer, '[') || CHECK(parser->buffer, ']')
            || CHECK(parser->buffer, '%'))
    {
        /* Check if it is a URI-escape sequence. */

        if (CHECK(parser->buffer, '%')) {
            if (!STRING_EXTEND(parser, string))
                goto error;

            if (!yaml_parser_scan_uri_escapes(parser,
                        directive, start_mark, &string)) goto error;
        }
        else {
            if (!READ(parser, string)) goto error;
        }

        length ++;
        if (!CACHE(parser, 1)) goto error;
    }

    /* Check if the tag is non-empty. */

    if (!length) {
        if (!STRING_EXTEND(parser, string))
            goto error;

        yaml_parser_set_scanner_error(parser, directive ?
                "while parsing a %TAG directive" : "while parsing a tag",
                start_mark, "did not find expected tag URI");
        goto error;
    }

    *uri = string.start;

    return 1;

error:
    STRING_DEL(parser, string);
    return 0;
}

/*
 * Decode an URI-escape sequence corresponding to a single UTF-8 character.
 */

static int
yaml_parser_scan_uri_escapes(yaml_parser_t *parser, int directive,
        yaml_mark_t start_mark, yaml_string_t *string)
{
    int width = 0;

    /* Decode the required number of characters. */

    do {

        unsigned char octet = 0;

        /* Check for a URI-escaped octet. */

        if (!CACHE(parser, 3)) return 0;

        if (!(CHECK(parser->buffer, '%')
                    && IS_HEX_AT(parser->buffer, 1)
                    && IS_HEX_AT(parser->buffer, 2))) {
            return yaml_parser_set_scanner_error(parser, directive ?
                    "while parsing a %TAG directive" : "while parsing a tag",
                    start_mark, "did not find URI escaped octet");
        }

        /* Get the octet. */

        octet = (AS_HEX_AT(parser->buffer, 1) << 4) + AS_HEX_AT(parser->buffer, 2);

        /* If it is the leading octet, determine the length of the UTF-8 sequence. */

        if (!width)
        {
            width = (octet & 0x80) == 0x00 ? 1 :
                    (octet & 0xE0) == 0xC0 ? 2 :
                    (octet & 0xF0) == 0xE0 ? 3 :
                    (octet & 0xF8) == 0xF0 ? 4 : 0;
            if (!width) {
                return yaml_parser_set_scanner_error(parser, directive ?
                        "while parsing a %TAG directive" : "while parsing a tag",
                        start_mark, "found an incorrect leading UTF-8 octet");
            }
        }
        else
        {
            /* Check if the trailing octet is correct. */

            if ((octet & 0xC0) != 0x80) {
                return yaml_parser_set_scanner_error(parser, directive ?
                        "while parsing a %TAG directive" : "while parsing a tag",
                        start_mark, "found an incorrect trailing UTF-8 octet");
            }
        }

        /* Copy the octet and move the pointers. */

        *(string->pointer++) = octet;
        SKIP(parser);
        SKIP(parser);
        SKIP(parser);

    } while (--width);

    return 1;
}

/*
 * Scan a block scalar.
 */

static int
yaml_parser_scan_block_scalar(yaml_parser_t *parser, yaml_token_t *token,
        int literal)
{
    yaml_mark_t start_mark;
    yaml_mark_t end_mark;
    yaml_string_t string = NULL_STRING;
    yaml_string_t leading_break = NULL_STRING;
    yaml_string_t trailing_breaks = NULL_STRING;
    int chomping = 0;
    int increment = 0;
    int indent = 0;
    int leading_blank = 0;
    int trailing_blank = 0;

    if (!STRING_INIT(parser, string, INITIAL_STRING_SIZE)) goto error;
    if (!STRING_INIT(parser, leading_break, INITIAL_STRING_SIZE)) goto error;
    if (!STRING_INIT(parser, trailing_breaks, INITIAL_STRING_SIZE)) goto error;

    /* Eat the indicator '|' or '>'. */

    start_mark = parser->mark;

    SKIP(parser);

    /* Scan the additional block scalar indicators. */

    if (!CACHE(parser, 1)) goto error;

    /* Check for a chomping indicator. */

    if (CHECK(parser->buffer, '+') || CHECK(parser->buffer, '-'))
    {
        /* Set the chomping method and eat the indicator. */

        chomping = CHECK(parser->buffer, '+') ? +1 : -1;

        SKIP(parser);

        /* Check for an indentation indicator. */

        if (!CACHE(parser, 1)) goto error;

        if (IS_DIGIT(parser->buffer))
        {
            /* Check that the indentation is greater than 0. */

            if (CHECK(parser->buffer, '0')) {
                yaml_parser_set_scanner_error(parser, "while scanning a block scalar",
                        start_mark, "found an indentation indicator equal to 0");
                goto error;
            }

            /* Get the indentation level and eat the indicator. */

            increment = AS_DIGIT(parser->buffer);

            SKIP(parser);
        }
    }

    /* Do the same as above, but in the opposite order. */

    else if (IS_DIGIT(parser->buffer))
    {
        if (CHECK(parser->buffer, '0')) {
            yaml_parser_set_scanner_error(parser, "while scanning a block scalar",
                    start_mark, "found an indentation indicator equal to 0");
            goto error;
        }

        increment = AS_DIGIT(parser->buffer);

        SKIP(parser);

        if (!CACHE(parser, 1)) goto error;

        if (CHECK(parser->buffer, '+') || CHECK(parser->buffer, '-')) {
            chomping = CHECK(parser->buffer, '+') ? +1 : -1;

            SKIP(parser);
        }
    }

    /* Eat whitespaces and comments to the end of the line. */

    if (!CACHE(parser, 1)) goto error;

    while (IS_BLANK(parser->buffer)) {
        SKIP(parser);
        if (!CACHE(parser, 1)) goto error;
    }

    if (CHECK(parser->buffer, '#')) {
        while (!IS_BREAKZ(parser->buffer)) {
            SKIP(parser);
            if (!CACHE(parser, 1)) goto error;
        }
    }

    /* Check if we are at the end of the line. */

    if (!IS_BREAKZ(parser->buffer)) {
        yaml_parser_set_scanner_error(parser, "while scanning a block scalar",
                start_mark, "did not find expected comment or line break");
        goto error;
    }

    /* Eat a line break. */

    if (IS_BREAK(parser->buffer)) {
        if (!CACHE(parser, 2)) goto error;
        SKIP_LINE(parser);
    }

    end_mark = parser->mark;

    /* Set the indentation level if it was specified. */

    if (increment) {
        indent = parser->indent >= 0 ? parser->indent+increment : increment;
    }

    /* Scan the leading line breaks and determine the indentation level if needed. */

    if (!yaml_parser_scan_block_scalar_breaks(parser, &indent, &trailing_breaks,
                start_mark, &end_mark)) goto error;

    /* Scan the block scalar content. */

    if (!CACHE(parser, 1)) goto error;

    while ((int)parser->mark.column == indent && !(IS_Z(parser->buffer)))
    {
        /*
         * We are at the beginning of a non-empty line.
         */

        /* Is it a trailing whitespace? */

        trailing_blank = IS_BLANK(parser->buffer);

        /* Check if we need to fold the leading line break. */

        if (!literal && (*leading_break.start == '\n')
                && !leading_blank && !trailing_blank)
        {
            /* Do we need to join the lines by space? */

            if (*trailing_breaks.start == '\0') {
                if (!STRING_EXTEND(parser, string)) goto error;
                *(string.pointer ++) = ' ';
            }

            CLEAR(parser, leading_break);
        }
        else {
            if (!JOIN(parser, string, leading_break)) goto error;
            CLEAR(parser, leading_break);
        }

        /* Append the remaining line breaks. */

        if (!JOIN(parser, string, trailing_breaks)) goto error;
        CLEAR(parser, trailing_breaks);

        /* Is it a leading whitespace? */

        leading_blank = IS_BLANK(parser->buffer);

        /* Consume the current line. */

        while (!IS_BREAKZ(parser->buffer)) {
            if (!READ(parser, string)) goto error;
            if (!CACHE(parser, 1)) goto error;
        }

        /* Consume the line break. */

        if (!CACHE(parser, 2)) goto error;

        if (!READ_LINE(parser, leading_break)) goto error;

        /* Eat the following indentation spaces and line breaks. */

        if (!yaml_parser_scan_block_scalar_breaks(parser,
                    &indent, &trailing_breaks, start_mark, &end_mark)) goto error;
    }

    /* Chomp the tail. */

    if (chomping != -1) {
        if (!JOIN(parser, string, leading_break)) goto error;
    }
    if (chomping == 1) {
        if (!JOIN(parser, string, trailing_breaks)) goto error;
    }

    /* Create a token. */

    SCALAR_TOKEN_INIT(*token, string.start, string.pointer-string.start,
            literal ? YAML_LITERAL_SCALAR_STYLE : YAML_FOLDED_SCALAR_STYLE,
            start_mark, end_mark);

    STRING_DEL(parser, leading_break);
    STRING_DEL(parser, trailing_breaks);

    return 1;

error:
    STRING_DEL(parser, string);
    STRING_DEL(parser, leading_break);
    STRING_DEL(parser, trailing_breaks);

    return 0;
}

/*
 * Scan indentation spaces and line breaks for a block scalar.  Determine the
 * indentation level if needed.
 */

static int
yaml_parser_scan_block_scalar_breaks(yaml_parser_t *parser,
        int *indent, yaml_string_t *breaks,
        yaml_mark_t start_mark, yaml_mark_t *end_mark)
{
    int max_indent = 0;

    *end_mark = parser->mark;

    /* Eat the indentation spaces and line breaks. */

    while (1)
    {
        /* Eat the indentation spaces. */

        if (!CACHE(parser, 1)) return 0;

        while ((!*indent || (int)parser->mark.column < *indent)
                && IS_SPACE(parser->buffer)) {
            SKIP(parser);
            if (!CACHE(parser, 1)) return 0;
        }

        if ((int)parser->mark.column > max_indent)
            max_indent = (int)parser->mark.column;

        /* Check for a tab character messing the indentation. */

        if ((!*indent || (int)parser->mark.column < *indent)
                && IS_TAB(parser->buffer)) {
            return yaml_parser_set_scanner_error(parser, "while scanning a block scalar",
                    start_mark, "found a tab character where an indentation space is expected");
        }

        /* Have we found a non-empty line? */

        if (!IS_BREAK(parser->buffer)) break;

        /* Consume the line break. */

        if (!CACHE(parser, 2)) return 0;
        if (!READ_LINE(parser, *breaks)) return 0;
        *end_mark = parser->mark;
    }

    /* Determine the indentation level if needed. */

    if (!*indent) {
        *indent = max_indent;
        if (*indent < parser->indent + 1)
            *indent = parser->indent + 1;
        if (*indent < 1)
            *indent = 1;
    }

   return 1;
}

/*
 * Scan a quoted scalar.
 */

static int
yaml_parser_scan_flow_scalar(yaml_parser_t *parser, yaml_token_t *token,
        int single)
{
    yaml_mark_t start_mark;
    yaml_mark_t end_mark;
    yaml_string_t string = NULL_STRING;
    yaml_string_t leading_break = NULL_STRING;
    yaml_string_t trailing_breaks = NULL_STRING;
    yaml_string_t whitespaces = NULL_STRING;
    int leading_blanks;

    if (!STRING_INIT(parser, string, INITIAL_STRING_SIZE)) goto error;
    if (!STRING_INIT(parser, leading_break, INITIAL_STRING_SIZE)) goto error;
    if (!STRING_INIT(parser, trailing_breaks, INITIAL_STRING_SIZE)) goto error;
    if (!STRING_INIT(parser, whitespaces, INITIAL_STRING_SIZE)) goto error;

    /* Eat the left quote. */

    start_mark = parser->mark;

    SKIP(parser);

    /* Consume the content of the quoted scalar. */

    while (1)
    {
        /* Check that there are no document indicators at the beginning of the line. */

        if (!CACHE(parser, 4)) goto error;

        if (parser->mark.column == 0 &&
            ((CHECK_AT(parser->buffer, '-', 0) &&
              CHECK_AT(parser->buffer, '-', 1) &&
              CHECK_AT(parser->buffer, '-', 2)) ||
             (CHECK_AT(parser->buffer, '.', 0) &&
              CHECK_AT(parser->buffer, '.', 1) &&
              CHECK_AT(parser->buffer, '.', 2))) &&
            IS_BLANKZ_AT(parser->buffer, 3))
        {
            yaml_parser_set_scanner_error(parser, "while scanning a quoted scalar",
                    start_mark, "found unexpected document indicator");
            goto error;
        }

        /* Check for EOF. */

        if (IS_Z(parser->buffer)) {
            yaml_parser_set_scanner_error(parser, "while scanning a quoted scalar",
                    start_mark, "found unexpected end of stream");
            goto error;
        }

        /* Consume non-blank characters. */

        if (!CACHE(parser, 2)) goto error;

        leading_blanks = 0;

        while (!IS_BLANKZ(parser->buffer))
        {
            /* Check for an escaped single quote. */

            if (single && CHECK_AT(parser->buffer, '\'', 0)
                    && CHECK_AT(parser->buffer, '\'', 1))
            {
                if (!STRING_EXTEND(parser, string)) goto error;
                *(string.pointer++) = '\'';
                SKIP(parser);
                SKIP(parser);
            }

            /* Check for the right quote. */

            else if (CHECK(parser->buffer, single ? '\'' : '"'))
            {
                break;
            }

            /* Check for an escaped line break. */

            else if (!single && CHECK(parser->buffer, '\\')
                    && IS_BREAK_AT(parser->buffer, 1))
            {
                if (!CACHE(parser, 3)) goto error;
                SKIP(parser);
                SKIP_LINE(parser);
                leading_blanks = 1;
                break;
            }

            /* Check for an escape sequence. */

            else if (!single && CHECK(parser->buffer, '\\'))
            {
                size_t code_length = 0;

                if (!STRING_EXTEND(parser, string)) goto error;

                /* Check the escape character. */

                switch (parser->buffer.pointer[1])
                {
                    case '0':
                        *(string.pointer++) = '\0';
                        break;

                    case 'a':
                        *(string.pointer++) = '\x07';
                        break;

                    case 'b':
                        *(string.pointer++) = '\x08';
                        break;

                    case 't':
                    case '\t':
                        *(string.pointer++) = '\x09';
                        break;

                    case 'n':
                        *(string.pointer++) = '\x0A';
                        break;

                    case 'v':
                        *(string.pointer++) = '\x0B';
                        break;

                    case 'f':
                        *(string.pointer++) = '\x0C';
                        break;

                    case 'r':
                        *(string.pointer++) = '\x0D';
                        break;

                    case 'e':
                        *(string.pointer++) = '\x1B';
                        break;

                    case ' ':
                        *(string.pointer++) = '\x20';
                        break;

                    case '"':
                        *(string.pointer++) = '"';
                        break;

                    case '/':
                        *(string.pointer++) = '/';
                        break;

                    case '\\':
                        *(string.pointer++) = '\\';
                        break;

                    case 'N':   /* NEL (#x85) */
                        *(string.pointer++) = '\xC2';
                        *(string.pointer++) = '\x85';
                        break;

                    case '_':   /* #xA0 */
                        *(string.pointer++) = '\xC2';
                        *(string.pointer++) = '\xA0';
                        break;

                    case 'L':   /* LS (#x2028) */
                        *(string.pointer++) = '\xE2';
                        *(string.pointer++) = '\x80';
                        *(string.pointer++) = '\xA8';
                        break;

                    case 'P':   /* PS (#x2029) */
                        *(string.pointer++) = '\xE2';
                        *(string.pointer++) = '\x80';
                        *(string.pointer++) = '\xA9';
                        break;

                    case 'x':
                        code_length = 2;
                        break;

                    case 'u':
                        code_length = 4;
                        break;

                    case 'U':
                        code_length = 8;
                        break;

                    default:
                        yaml_parser_set_scanner_error(parser, "while parsing a quoted scalar",
                                start_mark, "found unknown escape character");
                        goto error;
                }

                SKIP(parser);
                SKIP(parser);

                /* Consume an arbitrary escape code. */

                if (code_length)
                {
                    unsigned int value = 0;
                    size_t k;

                    /* Scan the character value. */

                    if (!CACHE(parser, code_length)) goto error;

                    for (k = 0; k < code_length; k ++) {
                        if (!IS_HEX_AT(parser->buffer, k)) {
                            yaml_parser_set_scanner_error(parser, "while parsing a quoted scalar",
                                    start_mark, "did not find expected hexdecimal number");
                            goto error;
                        }
                        value = (value << 4) + AS_HEX_AT(parser->buffer, k);
                    }

                    /* Check the value and write the character. */

                    if ((value >= 0xD800 && value <= 0xDFFF) || value > 0x10FFFF) {
                        yaml_parser_set_scanner_error(parser, "while parsing a quoted scalar",
                                start_mark, "found invalid Unicode character escape code");
                        goto error;
                    }

                    if (value <= 0x7F) {
                        *(string.pointer++) = value;
                    }
                    else if (value <= 0x7FF) {
                        *(string.pointer++) = 0xC0 + (value >> 6);
                        *(string.pointer++) = 0x80 + (value & 0x3F);
                    }
                    else if (value <= 0xFFFF) {
                        *(string.pointer++) = 0xE0 + (value >> 12);
                        *(string.pointer++) = 0x80 + ((value >> 6) & 0x3F);
                        *(string.pointer++) = 0x80 + (value & 0x3F);
                    }
                    else {
                        *(string.pointer++) = 0xF0 + (value >> 18);
                        *(string.pointer++) = 0x80 + ((value >> 12) & 0x3F);
                        *(string.pointer++) = 0x80 + ((value >> 6) & 0x3F);
                        *(string.pointer++) = 0x80 + (value & 0x3F);
                    }

                    /* Advance the pointer. */

                    for (k = 0; k < code_length; k ++) {
                        SKIP(parser);
                    }
                }
            }

            else
            {
                /* It is a non-escaped non-blank character. */

                if (!READ(parser, string)) goto error;
            }

            if (!CACHE(parser, 2)) goto error;
        }

        /* Check if we are at the end of the scalar. */

        /* Fix for crash unitialized value crash
         * Credit for the bug and input is to OSS Fuzz
         * Credit for the fix to Alex Gaynor
         */
        if (!CACHE(parser, 1)) goto error;
        if (CHECK(parser->buffer, single ? '\'' : '"'))
            break;

        /* Consume blank characters. */

        if (!CACHE(parser, 1)) goto error;

        while (IS_BLANK(parser->buffer) || IS_BREAK(parser->buffer))
        {
            if (IS_BLANK(parser->buffer))
            {
                /* Consume a space or a tab character. */

                if (!leading_blanks) {
                    if (!READ(parser, whitespaces)) goto error;
                }
                else {
                    SKIP(parser);
                }
            }
            else
            {
                if (!CACHE(parser, 2)) goto error;

                /* Check if it is a first line break. */

                if (!leading_blanks)
                {
                    CLEAR(parser, whitespaces);
                    if (!READ_LINE(parser, leading_break)) goto error;
                    leading_blanks = 1;
                }
                else
                {
                    if (!READ_LINE(parser, trailing_breaks)) goto error;
                }
            }
            if (!CACHE(parser, 1)) goto error;
        }

        /* Join the whitespaces or fold line breaks. */

        if (leading_blanks)
        {
            /* Do we need to fold line breaks? */

            if (leading_break.start[0] == '\n') {
                if (trailing_breaks.start[0] == '\0') {
                    if (!STRING_EXTEND(parser, string)) goto error;
                    *(string.pointer++) = ' ';
                }
                else {
                    if (!JOIN(parser, string, trailing_breaks)) goto error;
                    CLEAR(parser, trailing_breaks);
                }
                CLEAR(parser, leading_break);
            }
            else {
                if (!JOIN(parser, string, leading_break)) goto error;
                if (!JOIN(parser, string, trailing_breaks)) goto error;
                CLEAR(parser, leading_break);
                CLEAR(parser, trailing_breaks);
            }
        }
        else
        {
            if (!JOIN(parser, string, whitespaces)) goto error;
            CLEAR(parser, whitespaces);
        }
    }

    /* Eat the right quote. */

    SKIP(parser);

    end_mark = parser->mark;

    /* Create a token. */

    SCALAR_TOKEN_INIT(*token, string.start, string.pointer-string.start,
            single ? YAML_SINGLE_QUOTED_SCALAR_STYLE : YAML_DOUBLE_QUOTED_SCALAR_STYLE,
            start_mark, end_mark);

    STRING_DEL(parser, leading_break);
    STRING_DEL(parser, trailing_breaks);
    STRING_DEL(parser, whitespaces);

    return 1;

error:
    STRING_DEL(parser, string);
    STRING_DEL(parser, leading_break);
    STRING_DEL(parser, trailing_breaks);
    STRING_DEL(parser, whitespaces);

    return 0;
}

/*
 * Scan a plain scalar.
 */

static int
yaml_parser_scan_plain_scalar(yaml_parser_t *parser, yaml_token_t *token)
{
    yaml_mark_t start_mark;
    yaml_mark_t end_mark;
    yaml_string_t string = NULL_STRING;
    yaml_string_t leading_break = NULL_STRING;
    yaml_string_t trailing_breaks = NULL_STRING;
    yaml_string_t whitespaces = NULL_STRING;
    int leading_blanks = 0;
    int indent = parser->indent+1;

    if (!STRING_INIT(parser, string, INITIAL_STRING_SIZE)) goto error;
    if (!STRING_INIT(parser, leading_break, INITIAL_STRING_SIZE)) goto error;
    if (!STRING_INIT(parser, trailing_breaks, INITIAL_STRING_SIZE)) goto error;
    if (!STRING_INIT(parser, whitespaces, INITIAL_STRING_SIZE)) goto error;

    start_mark = end_mark = parser->mark;

    /* Consume the content of the plain scalar. */

    while (1)
    {
        /* Check for a document indicator. */

        if (!CACHE(parser, 4)) goto error;

        if (parser->mark.column == 0 &&
            ((CHECK_AT(parser->buffer, '-', 0) &&
              CHECK_AT(parser->buffer, '-', 1) &&
              CHECK_AT(parser->buffer, '-', 2)) ||
             (CHECK_AT(parser->buffer, '.', 0) &&
              CHECK_AT(parser->buffer, '.', 1) &&
              CHECK_AT(parser->buffer, '.', 2))) &&
            IS_BLANKZ_AT(parser->buffer, 3)) break;

        /* Check for a comment. */

        if (CHECK(parser->buffer, '#'))
            break;

        /* Consume non-blank characters. */

        while (!IS_BLANKZ(parser->buffer))
        {
            /* Check for "x:" + one of ',?[]{}' in the flow context. TODO: Fix the test "spec-08-13".
             * This is not completely according to the spec
             * See http://yaml.org/spec/1.1/#id907281 9.1.3. Plain
             */

            if (parser->flow_level
                    && CHECK(parser->buffer, ':')
                    && (
                        CHECK_AT(parser->buffer, ',', 1)
                        || CHECK_AT(parser->buffer, '?', 1)
                        || CHECK_AT(parser->buffer, '[', 1)
                        || CHECK_AT(parser->buffer, ']', 1)
                        || CHECK_AT(parser->buffer, '{', 1)
                        || CHECK_AT(parser->buffer, '}', 1)
                    )
                    ) {
                yaml_parser_set_scanner_error(parser, "while scanning a plain scalar",
                        start_mark, "found unexpected ':'");
                goto error;
            }

            /* Check for indicators that may end a plain scalar. */

            if ((CHECK(parser->buffer, ':') && IS_BLANKZ_AT(parser->buffer, 1))
                    || (parser->flow_level &&
                        (CHECK(parser->buffer, ',')
                         || CHECK(parser->buffer, '?') || CHECK(parser->buffer, '[')
                         || CHECK(parser->buffer, ']') || CHECK(parser->buffer, '{')
                         || CHECK(parser->buffer, '}'))))
                break;

            /* Check if we need to join whitespaces and breaks. */

            if (leading_blanks || whitespaces.start != whitespaces.pointer)
            {
                if (leading_blanks)
                {
                    /* Do we need to fold line breaks? */

                    if (leading_break.start[0] == '\n') {
                        if (trailing_breaks.start[0] == '\0') {
                            if (!STRING_EXTEND(parser, string)) goto error;
                            *(string.pointer++) = ' ';
                        }
                        else {
                            if (!JOIN(parser, string, trailing_breaks)) goto error;
                            CLEAR(parser, trailing_breaks);
                        }
                        CLEAR(parser, leading_break);
                    }
                    else {
                        if (!JOIN(parser, string, leading_break)) goto error;
                        if (!JOIN(parser, string, trailing_breaks)) goto error;
                        CLEAR(parser, leading_break);
                        CLEAR(parser, trailing_breaks);
                    }

                    leading_blanks = 0;
                }
                else
                {
                    if (!JOIN(parser, string, whitespaces)) goto error;
                    CLEAR(parser, whitespaces);
                }
            }

            /* Copy the character. */

            if (!READ(parser, string)) goto error;

            end_mark = parser->mark;

            if (!CACHE(parser, 2)) goto error;
        }

        /* Is it the end? */

        if (!(IS_BLANK(parser->buffer) || IS_BREAK(parser->buffer)))
            break;

        /* Consume blank characters. */

        if (!CACHE(parser, 1)) goto error;

        while (IS_BLANK(parser->buffer) || IS_BREAK(parser->buffer))
        {
            if (IS_BLANK(parser->buffer))
            {
                /* Check for tab character that abuse indentation. */

                if (leading_blanks && (int)parser->mark.column < indent
                        && IS_TAB(parser->buffer)) {
                    yaml_parser_set_scanner_error(parser, "while scanning a plain scalar",
                            start_mark, "found a tab character that violate indentation");
                    goto error;
                }

                /* Consume a space or a tab character. */

                if (!leading_blanks) {
                    if (!READ(parser, whitespaces)) goto error;
                }
                else {
                    SKIP(parser);
                }
            }
            else
            {
                if (!CACHE(parser, 2)) goto error;

                /* Check if it is a first line break. */

                if (!leading_blanks)
                {
                    CLEAR(parser, whitespaces);
                    if (!READ_LINE(parser, leading_break)) goto error;
                    leading_blanks = 1;
                }
                else
                {
                    if (!READ_LINE(parser, trailing_breaks)) goto error;
                }
            }
            if (!CACHE(parser, 1)) goto error;
        }

        /* Check indentation level. */

        if (!parser->flow_level && (int)parser->mark.column < indent)
            break;
    }

    /* Create a token. */

    SCALAR_TOKEN_INIT(*token, string.start, string.pointer-string.start,
            YAML_PLAIN_SCALAR_STYLE, start_mark, end_mark);

    /* Note that we change the 'simple_key_allowed' flag. */

    if (leading_blanks) {
        parser->simple_key_allowed = 1;
    }

    STRING_DEL(parser, leading_break);
    STRING_DEL(parser, trailing_breaks);
    STRING_DEL(parser, whitespaces);

    return 1;

error:
    STRING_DEL(parser, string);
    STRING_DEL(parser, leading_break);
    STRING_DEL(parser, trailing_breaks);
    STRING_DEL(parser, whitespaces);

    return 0;
}
