/* SPDX-License-Identifier: MPL-2.0 */
/* LOOM-8's Bluetooth: audio out to headphones and speakers (plat_bta_*), and the Bluetooth MIDI device it connects
 * to (fm1-base's lib/btdev.h). Setup shows both; GLO's SEL list has BT AUDIO (on or off) and AUDIO DEVICE and MIDI
 * DEVICE, whose SEL opens the list of devices (pages.c list_bt_audio, list_bt_dev; ui.c bt_poll scans while a list is up and says what happened).
 * Whether MIDI and audio are on are the project's BT_MIDI_OFF and BT_AUDIO_ON bits, carried from project to project as the theme is: at power-on
 * LOOM-8 switches Bluetooth MIDI off again (once the radio is up) when it was off, and connects the last audio
 * device again when audio was on. Included by pages.c, after its string helpers. */

#define BT_AUDIO_ON 64u                              /* proj.rsv[2]: Bluetooth audio on (the last device at power-on) */
#define BT_MIDI_OFF 128u                             /* proj.rsv[2]: Bluetooth MIDI off (no advertising, no links) */
#define BT_ROWS 8                                    /* the devices a list shows (remembered first, for audio) */

static struct {
    uint8_t want;                                    /* Bluetooth audio on, as the player set it */
    uint8_t midi;                                    /* Bluetooth MIDI on, as the player set it */
    uint8_t boot;                                    /* power-on: connect the last device once the radio is up */
    uint8_t cur;                                     /* the remembered device asked last (its row shows the state) */
    uint8_t was;                                     /* the audio's state last frame (what changed is said) */
    uint8_t list;                                    /* the list up last frame: 0, 1 audio, 2 MIDI devices */
    uint32_t t0;                                     /* when the list opened (MIDI: "none found yet" after 8 s) */
    int32_t pin;                                     /* the pairing code said last (-1 none) */
} bt;
static char bt_nm[BT_ROWS][21];                      /* the list's devices' names (its rows point here) */
static uint8_t bt_ix[BT_ROWS];                       /* each one's index: found, or 0x80 | remembered */

#if FM1B_BT_AUDIO
#define BTA(f) plat_bta_##f
#else
/* no Bluetooth audio in this build (the simulator, the browser): a stand-in. The simulator finds "Speaker" half a
 * second into a scan (10 s long) and streams to it 0.3 s after a connect; the browser finds nothing */
#define BTA(f) sim_bta_##f
static struct { uint8_t on, st, found, nk; uint32_t t; char k[4][17]; } sb;
static void sim_bta_on(int on)
{
    if (!on == !sb.on)
        return;
    sb.on = (uint8_t)(on != 0);
    sb.st = on ? PLAT_BTA_NONE : PLAT_BTA_OFF;
}
static void sim_bta_scan(int on)
{
    if (!sb.on)
        return;
    if (on) {
        sb.st = PLAT_BTA_SCANNING;
        sb.found = 0;
        sb.t = plat_ms();
    } else if (sb.st == PLAT_BTA_SCANNING)
        sb.st = PLAT_BTA_NONE;
}
static int sim_bta_state(void)
{
    uint32_t dt = plat_ms() - sb.t;
#ifndef PLAT_WEB
    if (sb.st == PLAT_BTA_SCANNING && dt >= 500u)
        sb.found = 1;
#endif
    if (sb.st == PLAT_BTA_SCANNING && dt >= 10000u)
        sb.st = PLAT_BTA_NONE;
    if (sb.st == PLAT_BTA_PAGING && dt >= 300u)
        sb.st = PLAT_BTA_STREAMING;
    return sb.st;
}
static int sim_bta_found(int i, char *name)
{
    (void)sim_bta_state();
    if (i != 0 || !sb.found)
        return 0;
    if (name)
        s_cat(name, "Speaker");
    return 1;
}
static int sim_bta_known(int i, char *name)
{
    if (i < 0 || i >= sb.nk)
        return 0;
    if (name)
        s_cat(name, sb.k[i]);
    return 1;
}
static void sim_bta_pick(int i)
{
    if (!sb.on || !sim_bta_found(i, 0))
        return;
    if (!sb.nk || !s_eq(sb.k[0], "Speaker")) {
        for (int j = sb.nk < 4 ? sb.nk++ : 3; j > 0; j--)
            memcpy(sb.k[j], sb.k[j - 1], sizeof sb.k[0]);
        memset(sb.k[0], 0, sizeof sb.k[0]);
        s_cat(sb.k[0], "Speaker");
    }
    sb.st = PLAT_BTA_PAGING;
    sb.t = plat_ms();
}
static int sim_bta_reconnect(int i)
{
    if (!sb.on || i < 0 || i >= sb.nk)
        return -1;
    sb.st = PLAT_BTA_PAGING;
    sb.t = plat_ms();
    return 0;
}
static void sim_bta_forget(int i)
{
    if (i < 0 || i >= sb.nk)
        return;
    for (sb.nk--; i < sb.nk; i++)
        memcpy(sb.k[i], sb.k[i + 1], sizeof sb.k[0]);
}
static void sim_bta_hangup(void)
{
    if (sb.on)
        sb.st = PLAT_BTA_NONE;
}
#endif

#ifdef PLAT_HOST
static int sim_passkey = -1;                         /* the script's "passkey N" (host.c): a pairing waiting */
#endif
#if defined(PLAT_HOST) && !defined(PLAT_WEB)
#define BT_PASSKEY() sim_passkey
#else
#define BT_PASSKEY() plat_ble_passkey()
#endif

static int bt_linking(int st) { return st >= PLAT_BTA_PAGING && st <= PLAT_BTA_SETUP; }
/* the audio's state in a word: a row's value, the screen reader's too */
static const char *bt_state_word(int st)
{
    return st == PLAT_BTA_STREAMING ? "playing" : bt_linking(st) ? "connecting" : st == PLAT_BTA_REFUSED ? "refused"
         : st == PLAT_BTA_SCANNING ? "looking" : st == PLAT_BTA_OFF ? "off" : "not connected";
}
/* Setup's "BT audio": off, the device playing, connecting, not connected... */
static void bt_audio_text(char *t)
{
    int st = BTA(state)();
    *t = 0;
    if (st == PLAT_BTA_STREAMING && BTA(known)(bt.cur, t))
        return;
    if (bt.want && bt.boot) {                        /* (power-on: the radio still starting) */
        s_cat(t, "connecting");
        return;
    }
    s_cat(t, !bt.want && st != PLAT_BTA_OFF && !bt_linking(st) ? "off" : bt_state_word(st));
}
/* GLO's AUDIO DEVICE: the device playing, connecting, or "choose" (SEL opens the list) */
static void bt_adev_text(char *t)
{
    int st = BTA(state)();
    *t = 0;
    if (st == PLAT_BTA_STREAMING && BTA(known)(bt.cur, t))
        return;
    s_cat(t, bt_linking(st) || (bt.want && bt.boot) ? "connecting" : "choose");
}
/* the Bluetooth MIDI device: connected, connecting, or idle (Setup "none", GLO's list "choose"); a computer's link
 * leaves no room for one */
static void bt_dev_text(char *t, const char *idle, const char *off)
{
    int d = plat_ble_device();
    *t = 0;
    s_cat(t, !bt.midi ? off : d == BLE_DEV_READY ? btdev_name() : d == BLE_DEV_CONNECTING || d == BLE_DEV_SEARCHING ? "connecting"
             : plat_ble_linked() ? "computer connected" : idle);
}
static void bt_keep(int on)                          /* the settings, into the project (saved as any change) */
{
    bt.want = (uint8_t)(on != 0);
    proj.rsv[2] = (uint8_t)(on ? proj.rsv[2] | BT_AUDIO_ON : proj.rsv[2] & ~BT_AUDIO_ON);
    proj.rsv[2] = (uint8_t)(bt.midi ? proj.rsv[2] & ~BT_MIDI_OFF : proj.rsv[2] | BT_MIDI_OFF);
}
