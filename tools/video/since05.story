# LOOM-8: what's new since 0.5 (python3 tools/video.py tools/video/since05.story OUT.mp4)
head what's new since 0.5

# ---- hidden setup: content and starting state, none of this filmed ----
hide
tap GLO
turn SELECT 5
turn K4 2
tap HOME
hits 1 1 1 x...x...x...x...
hits 1 1 2 ....x.......x...
hits 1 1 3 x.x.x.x.x.x.x.x.
spin ALGO 1
step 2 1 1 60
step 2 1 5 64
step 2 1 9 67
step 2 1 13 64
knob 2 send1 110
knob 11 engine 0
spin ALGO 1
tap EDIT
tap SEL
turn ALGO 6 10
tap SEL
step 3 1 1 53
step 3 1 5 55
step 3 1 9 57
step 3 1 13 59
tap HOME
spin ALGO -2
tap PLAY
show

theme b/w
card LOOM-8 | since 0.5
wait 4800
card -
cap New look | the b/w theme: the original black, white and red
wait 6400

# ---- theme cycling, the real Setup gesture ----
cap Setup | GLO, then SELECT to its last page
tap GLO
turn SELECT 5 180
expect page Setup
wait 3200
cap THEME | KNOB 4 turns it
wait 2400
theme blueprint
spin K4 -1
wait 6400
cap Blueprint | tape deck, blueprint, b/w
wait 4000
theme b/w
spin K4 1
wait 2400
cap Back to b/w | it stays for the rest
wait 5600
tap HOME

# ---- FX: chorus and phaser ----
cap New send effects | FX, then SELECT twice: FX I
tap FX
turn SELECT 2 180
expect page FX_I
wait 4000
cap Chorus | new since 0.5
wait 6400
turn PRESET 3 220
cap Phaser | new since 0.5
wait 7200
tap HOME

# ---- step tricks ----
cap Step tricks | hold a step, tap ARP
tap SEQ
wait 1600
key w0 down
wait 800
tap ARP
wait 1280
cap Multiply | KNOB 1 the trick, KNOB 2 its value
turn K1 3 180
wait 800
turn K2 2 180
wait 4800
key w0 up
cap One step, a ratchet | no extra steps written
wait 5120

# ---- motion recording ----
cap Motion recording | REC held while it plays, turn a knob
spin ALGO 1
tap EDIT
wait 1600
press REC
wait 320
spin K1 14 100
wait 1920
spin K1 -14 100
release REC
cap The sweep is written in | it plays back on its own
wait 6720
tap HOME

# ---- slicer ----
cap Slicer | any break, cut into playable steps
spin ALGO 1
tap SEQ
wait 9600
cap Eight slices on the keys | F3 to C4
wait 5120
tap HOME

# ---- weave, redone ----
cap Weave | one track borrows another's rhythm
spin ALGO -1
tap ARP
turn SELECT 3 180
expect page Weave
wait 2400
tapkey b0
wait 1120
tapkey w0
wait 1120
cap Rule: rhythm | the kit's beat, this track's notes
turn K3 2 180
wait 9600
cap Nothing is written | it plays woven live
wait 6400

card -
wait 640
card LOOM-8 | charlesvestal.github.io/fm1-loom
wait 6400
