#include "config.h"
#include "liblognorm.h"

struct err_cb_state {
        int called;
        int cookie_match;
        int saw_message;
};

static void
error_callback(void *cookie, const char *msg, size_t len)
{
        struct err_cb_state *state = (struct err_cb_state *) cookie;

        if(state != NULL) {
                state->called++;
                if(cookie == state) {
                        state->cookie_match = 1;
                }
                if(msg != NULL && len > 0) {
                        state->saw_message = 1;
                }
        }
}

int
main(void)
{
        static const char *const invalid_rulebase =
                "version=2\n"
                "rule=:%arr:tokenized:quux:some_non_existent_type%\n";
        struct err_cb_state state = {0, 0, 0};
        ln_ctx ctx = ln_initCtx();
        int ret = 1;

        if(ctx == NULL)
                return ret;

        if(ln_setErrMsgCB(ctx, error_callback, &state) != 0)
                goto done;

        ln_loadSamplesFromString(ctx, invalid_rulebase);

        if(state.called >= 1 && state.cookie_match == 1 && state.saw_message == 1)
                ret = 0;

done:
        ln_exitCtx(ctx);
        return ret;
}
