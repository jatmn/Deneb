/* SPDX-License-Identifier: MPL-2.0 */
#include "../src/df_bridge.c"
#include <assert.h>
int main(void)
{
    assert(!bridge_action_complete("connect", false, false, false, 0, ""));
    assert(!bridge_action_complete("connect", false, false, true, DF_STATE_CONNECTED, ""));
    assert(!bridge_action_complete("connect", true, false, true, DF_STATE_CONNECTED, ""));
    assert(!bridge_action_complete("connect", true, true, true, DF_STATE_RECONNECTING, ""));
    assert(bridge_action_complete("connect", true, true, true, DF_STATE_ENTER_PIN, "1234"));
    assert(bridge_action_complete("connect", true, true, true, DF_STATE_CONNECTED, ""));
    assert(!bridge_action_complete("disconnect", true, true, true, DF_STATE_CONNECTED, ""));
    assert(bridge_action_complete("disconnect", true, true, true, DF_STATE_DISCONNECTED, ""));
    assert(bridge_action_complete("status", false, false, true, DF_STATE_CONNECTED, ""));
    return 0;
}
