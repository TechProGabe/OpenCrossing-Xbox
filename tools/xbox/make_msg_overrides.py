#!/usr/bin/env python3
"""Write xbox/src/msg_overrides.inc: our own text for the game messages that
talk about a Game Boy Advance, which an Xbox can't connect (the user's
brief, 2026-10-29: "funny and meta"). xbox/src/xbox_msg.c hands them to
mMsg_LoadMsgData in place of the disc's text.

  python tools/xbox/make_msg_overrides.py [message_data.bin]

The text below is in tools/msg_tool.py's syntax (the one its unpack writes)
and is encoded with it, so control codes (pauses, colours, Kapp'n's
animations, the island's name) work as in the game's own lines. Only our
own words go into the .inc. Given the disc's message_data.bin (from
forest_2nd.arc, docs/ref/anbernic/game.md), it first checks that
msg_tool encodes a disc entry back to its exact bytes.
"""
import pathlib
import struct
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
import msg_tool  # noqa: E402

OUT = ROOT / 'xbox/src/msg_overrides.inc'

# Kapp'n leaving for the island (12407; 12617 is the version for a girl)
SAIL = """<<DEMONPC0 [00 00 0E]>>Righty now!<<PAUSE [05]>> Off we go!!<<PAUSE [0A]>>
Can't hook a <<COLORCHARS [19 8C DC 10]>>Game Boy Advance
up to an Xbox, so we'll
sail yer hard drive instead!<<BTN>>
<<MSGCLEAR>>To <<STR_FREE0>> Island!<<PAUSE [0A]>>
Mind the bad sectors,
matey!!<<BTN>>
<<MSGCLEAR>>Now! <<PAUSE [0A]>>Clean out yer ears an'
listen to me shanty<<PAUSE [03]>>
o' love, ye wee wriggler!!
<<MSGEND>>"""

# arriving (12410 / 12620): page 2 told you to switch the GBA off
ARRIVE = """<<DEMONPC0 [00 00 0E]>><<STR_FREE0>> Island, ho!!<<PAUSE [10]>>
<<DEMONPC0 [00 00 17]>>Well now, <<PAUSE [03]>>that were some
bumpy ride, wouldn't ye say?<<PAUSE [0A]>>
Aye, almost lost me vittles!
<<BTN>>
<<MSGCLEAR>><<DEMONPC0 [00 00 FF]>>An' don't ye go switchin'<<PAUSE [06]>>
off that Xbox, now.<<PAUSE [0A]>> From
here it's one long swim
back to the dashboard!
<<MSGEND>>"""

# the post office's e-Reader Transfer Machine (src/actor/ac_pterminal.c on
# the Xbox): 15854 every time, 15855 the first time (Super Mario Bros. goes
# in the pockets), 15836 when that gift doesn't fit
EREADER = """Welcome, <<STR_PLAYERNAME>>.<<PAUSE [0C]>>
Thank you for using the
<<COLORCHARS [E1 1E D7 19]>>e-Reader Transfer Machine.<<BTN>>
<<MSGCLEAR>>...Wait a second.<<PAUSE [0A]>>
How am I supposed to read
a card on an Xbox?<<BTN>>
<<MSGCLEAR>>Sorry, pal.<<PAUSE [08]>>
That's not gonna work."""

EREADER_GIFT = EREADER + """<<BTN>>
<<MSGCLEAR>>Here, take this for
the trouble:<<PAUSE [08]>> <<COLORCHARS [19 8C DC 10]>>Super Mario Bros!<<PAUSE [0A]>>
Don't tell Tom Nook.
<<MSGEND>>"""

EREADER_FULL = EREADER + """<<BTN>>
<<MSGCLEAR>>I'd give you something for
the trouble, but your
pockets are full.<<PAUSE [08]>>
Come back with some room!
<<MSGEND>>"""

# the Porter, asked for a trip (src/actor/ac_station_clip.c_inc on the
# Xbox): 0x094A every time, 0x0946 the first time (The Legend of Zelda goes
# in the pockets), 0x0947 when that gift doesn't fit. Card-error slots the
# Xbox never reaches any more.
PORTER = """<<DEMONPC0 [00 00 02]>>Eek eek! A trip?<<PAUSE [0A]>>
I'd love to send you off,
but for that I need a
<<COLORCHARS [19 8C DC 14]>>GameCube Memory Card...<<BTN>>
<<MSGCLEAR>><<DEMONPC0 [00 00 17]>>...and I've checked this
whole Xbox, <<STR_PLAYERNAME>>!<<PAUSE [0A]>>
There's no slot for one.
Not even behind the jewel!<<BTN>>
<<MSGCLEAR>><<DEMONPC0 [00 00 FF]>>So no trains out, I'm afraid.
But don't worry: Tom Nook
will grow his store all on
his own. Eek eek!"""

PORTER_GIFT = PORTER + """<<BTN>>
<<MSGCLEAR>><<DEMONPC0 [00 00 02]>>Oh! And TechProGabe sends
his most sincere apologies
with this gift:<<PAUSE [0A]>>
<<COLORCHARS [19 8C DC 13]>>The Legend of Zelda!
<<MSGEND>>"""

PORTER_FULL = PORTER + """<<BTN>>
<<MSGCLEAR>>I'd give you something for
your trouble, but your
pockets are full! Come back
with some room. Eek eek!
<<MSGEND>>"""

OVERRIDES = [(12407, SAIL), (12617, SAIL), (12410, ARRIVE), (12620, ARRIVE),
             (15854, EREADER + "\n<<MSGEND>>"), (15855, EREADER_GIFT), (15836, EREADER_FULL),
             (0x094A, PORTER + "\n<<MSGEND>>"), (0x0946, PORTER_GIFT), (0x0947, PORTER_FULL)]


def check_roundtrip(data_path):
    """msg_tool must turn a disc entry's own text back into its exact bytes"""
    data = pathlib.Path(data_path).read_bytes()
    table = pathlib.Path(data_path).with_name(pathlib.Path(data_path).stem + '_table.bin').read_bytes()
    ends = struct.unpack(f'>{len(table) // 4}I', table)
    # the table holds each entry's end; zero entries are empty
    last, spans = 0, {}
    for i, end in enumerate(ends):
        if end:
            spans[i] = (last, end)
            last = end
    for idx in (12407, 12410):
        a, b = spans[idx]
        raw = data[a:b]
        text = msg_tool.decode_entry(bytearray(raw), 0, len(raw), idx)
        body = text.split(']]\n', 1)[1].rstrip('\n')
        if bytes(msg_tool.encode_entry(body)) != raw:
            sys.exit(f'make_msg_overrides: msg_tool does not round-trip entry {idx}')
    print('make_msg_overrides: msg_tool round-trips the disc entries')


def main():
    if len(sys.argv) > 1:
        check_roundtrip(sys.argv[1])
    lines = ['/* msg_overrides.inc - our text for the game messages that mention a Game',
             ' * Boy Advance (xbox_msg.c). Generated by tools/xbox/make_msg_overrides.py',
             ' * from the text there; do not edit. */']
    for idx, text in OVERRIDES:
        b = msg_tool.encode_entry(text)
        if len(b) > 1536:
            sys.exit(f'{idx}: {len(b)} bytes, the message buffer holds 1536')
        lines.append(f'static const unsigned char k_msg_{idx}[{len(b)}] = {{')
        for i in range(0, len(b), 16):
            lines.append('    ' + ' '.join(f'0x{x:02X},' for x in b[i:i + 16]))
        lines.append('};')
    lines.append('static const MsgOverride k_msg_overrides[] = {')
    lines += [f'    {{ {idx}, k_msg_{idx}, sizeof k_msg_{idx} }},' for idx, _ in OVERRIDES]
    lines += ['};', '']
    OUT.write_text('\n'.join(lines), newline='\n')
    print(f'make_msg_overrides: {len(OVERRIDES)} messages -> {OUT.relative_to(ROOT)}')


if __name__ == '__main__':
    main()
