#include "config.h"
#include "liblognorm.h"
#include "lognorm.h"

struct err_cb_state {
        int called;
        int cookie_match;
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
        }

        (void) msg;
        (void) len;
}

int
main(void)
{
        struct err_cb_state state = {0, 0};
        ln_ctx ctx = ln_initCtx();
        int ret = 1;

        if(ctx == NULL)
                return ret;

        if(ln_setErrMsgCB(ctx, error_callback, &state) != 0)
                goto done;

        ln_errprintf(ctx, 0, "test message");

        if(state.called == 1 && state.cookie_match == 1)
                ret = 0;

done:
        ln_exitCtx(ctx);
        return ret;
}
