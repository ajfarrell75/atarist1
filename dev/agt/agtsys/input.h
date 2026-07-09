//======================================================================================================================
// 	[A]tari [G]ame [T]ools / dml 2016
//======================================================================================================================
//	Keyboard/Joystick/Mouse/Joypad interfacing
//----------------------------------------------------------------------------------------------------------------------

#ifndef input_h_
#define input_h_

// ---------------------------------------------------------------------------------------------------------------------
// Atari IKBD scancodes

enum ScanCode
{
	ScanCode_ESC = 0x01,
	//
	ScanCode_1 = 0x02,
	ScanCode_2,
	ScanCode_3,
	ScanCode_4,
	ScanCode_5,
	ScanCode_6,
	ScanCode_7,
	ScanCode_8,
	ScanCode_9,
	ScanCode_0,
	//
	ScanCode_MINUS = 0x0c,
	ScanCode_EQU = 0x0d,
	ScanCode_BS = 0x0e,
	ScanCode_TAB = 0x0f,
	//
	ScanCode_Q = 0x10,
	ScanCode_W,
	ScanCode_E,
	ScanCode_R,
	ScanCode_T,
	ScanCode_Y,
	ScanCode_U,
	ScanCode_I,
	ScanCode_O,
	ScanCode_P,
	//
	ScanCode_SBOPEN = 0x1a,
	ScanCode_SBCLOSE = 0x1b,
	ScanCode_RETURN = 0x1c,
	ScanCode_CTRL = 0x1d,
	//
	ScanCode_A = 0x1e,
	ScanCode_S,
	ScanCode_D,
	ScanCode_F,
	ScanCode_G,
	ScanCode_H,
	ScanCode_J,
	ScanCode_K,
	ScanCode_L,
	//
	ScanCode_SEMICOLON = 0x27,
	ScanCode_AP1 = 0x28,
	ScanCode_AT = ScanCode_AP1,
	ScanCode_AP2 = 0x29,
	ScanCode_LSHIFT = 0x2a,
	ScanCode_HASH = 0x2b,
	//
	ScanCode_Z = 0x2c,
	ScanCode_X,
	ScanCode_C,
	ScanCode_V,
	ScanCode_B,
	ScanCode_N,
	ScanCode_M,
	//
	ScanCode_COMMA = 0x33,
	ScanCode_DOT = 0x34,
	ScanCode_FSLASH = 0x35,
	ScanCode_RSHIFT = 0x36,
	// 37?
	ScanCode_ALT = 0x38,
	ScanCode_SPACE = 0x39,
	ScanCode_CAPS = 0x3a,
	//
	ScanCode_F1 = 0x3b,
	ScanCode_F2,
	ScanCode_F3,
	ScanCode_F4,
	ScanCode_F5,
	ScanCode_F6,
	ScanCode_F7,
	ScanCode_F8,
	ScanCode_F9,
	ScanCode_F10,
	// 45-46?
	ScanCode_CLRHOME = 0x47,
	ScanCode_UP = 0x48,
	// 49?
	ScanCode_KP_MINUS = 0x4a,
	ScanCode_LEFT = 0x4b,
	// 4c?
	ScanCode_RIGHT = 0x4d,
	ScanCode_KP_PLUS = 0x4e,
	// 4f?
	ScanCode_DOWN = 0x50,
	// 51?
	ScanCode_INS = 0x52,
	ScanCode_DEL = 0x53,
	// 54-5f?
	ScanCode_BSLASH = 0x60,
	ScanCode_UNDO = 0x61,
	ScanCode_HELP = 0x62,
	ScanCode_KP_BROPEN = 0x63,
	ScanCode_KP_BRCLOSE = 0x64,
	ScanCode_KP_FSLASH = 0x65,
	ScanCode_KP_STAR = 0x66,
	//
	ScanCode_KP_7 = 0x67,
	ScanCode_KP_8,
	ScanCode_KP_9,
	ScanCode_KP_4,
	ScanCode_KP_5,
	ScanCode_KP_6,
	ScanCode_KP_1,
	ScanCode_KP_2,
	ScanCode_KP_3,
	ScanCode_KP_0,
	//
	ScanCode_KP_DOT = 0x71,
	ScanCode_KP_ENTER = 0x72,
};

// JoyPad bit pattern:
// 1 4 7 * 3 6 9 # 2 5 8 0 o p c b a r l d u

enum
{
	JoyPad_N1		= (1<<20),
	JoyPad_N4		= (1<<19),
	JoyPad_N7		= (1<<18),
	JoyPad_STAR		= (1<<17),
//
	JoyPad_N3		= (1<<16),
	JoyPad_N6		= (1<<15),
	JoyPad_N9		= (1<<14),
	JoyPad_HASH		= (1<<13),
//
	JoyPad_N2		= (1<<12),
	JoyPad_N5		= (1<<11),
	JoyPad_N8		= (1<<10),
	JoyPad_N0		= (1<<9),
	
	JoyPad_O		= (1<<8),
	JoyPad_P		= (1<<7),
	
	JoyPad_C		= (1<<6),
	JoyPad_B		= (1<<5),
	JoyPad_A		= (1<<4),
	
	JoyPad_R		= (1<<3),
	JoyPad_L		= (1<<2),
	JoyPad_D		= (1<<1),
	JoyPad_U		= (1<<0),
};

// joypad buttons only
#define JoyPad_JOYMASK ( \
		JoyPad_A|JoyPad_B|JoyPad_C | \
		JoyPad_L|JoyPad_R|JoyPad_U|JoyPad_D \
		)

// joypad keypad/letters only
#define JoyPad_KEYMASK ( \
		JoyPad_P|JoyPad_O | \
		JoyPad_N0|JoyPad_N1|JoyPad_N2|JoyPad_N3|JoyPad_N4|JoyPad_N5|JoyPad_N6|JoyPad_N7|JoyPad_N9 | \
		JoyPad_HASH|JoyPad_STAR \
		)

extern "C" void AGT_SampleJoyPads();

// ---------------------------------------------------------------------------------------------------------------------

// table for translating scancodes into ASCII and special key enums
extern u8 scantokey[128];
// buffer holding instantaneous down/up state of every key (0 or nonzero)
extern "C" u8 key_states[128];
extern "C" u8 debounced_key_states[128*2];
extern "C" u8 debounced_key_presses[128];
extern "C" u8 debounced_key_releases[128];
// ringbuffer of key events
extern "C" u8 key_eventring[256];
// write position of eventring (written by IKBD interupt)
extern "C" volatile s16 key_eventhead;
// read position of eventring (read by game input services)
extern "C" s16 key_eventtail;

// mousebutton state
extern "C" volatile s16 buttons;
// ringbuffer of mousebutton events
extern "C" u8 msclick_eventring[256];
// write position of eventring (written by IKBD interupt)
extern "C" volatile s16 msclick_eventhead;
// read position of eventring (read by game input services)
extern "C" s16 msclick_eventtail;

// joystick 0 state
extern "C" volatile u8 joy0;
// ringbuffer of joy events
extern "C" u8 joy0_eventring[256];
// write position of eventring (written by IKBD interupt)
extern "C" volatile s16 joy0_eventhead;
// read position of eventring (read by game input services)
extern "C" s16 joy0_eventtail;

// joystick 1 state
extern "C" volatile u8 joy1;
// ringbuffer of joy events
extern "C" u8 joy1_eventring[256];
// write position of eventring (written by IKBD interupt)
extern "C" volatile s16 joy1_eventhead;
// read position of eventring (read by game input services)
extern "C" s16 joy1_eventtail;

// joypads
extern "C" volatile u32 joypad0;
extern "C" volatile u32 joypad1;

// ---------------------------------------------------------------------------------------------------------------------
// here is some example code to read & flush all pending events from rings
//
//{
//	s16 tail = key_eventtail;
//	while (tail != key_eventhead)
//	{
//		u8 kcode = (u8)(key_eventring[tail]);
//		tail = (tail + 1) & 255;
//
//		dosomething(kcode);
//	}
//	key_eventtail = tail;
//}

template<int c_size>
inline void clear(void *const _dst)
{
	int remaining = c_size;

	void *dst = _dst;

	s32 zero = 0;

	if (remaining & -4)
	{
		s32 *dst32 = (s32*)dst;

		while (remaining & -4)
		{
			__asm __volatile__
			(
				"move.l %1,(%0)+;"
				: "+a"(dst32)
				: "r"(zero)
				:
			);

			remaining -= 4;
		}

		dst = dst32;
	}

	if (remaining & -2)
	{
		s16 *dst16 = (s16*)dst;

		while (remaining & -2)
		{
			__asm __volatile__
			(
				"move.w %1,(%0)+;"
				: "+a"(dst16)
				: "r"(zero)
				:
			);

			remaining -= 2;
		}

		dst = dst16;
	}

	if (remaining)
	{
		s8 *dst8 = (s8*)dst;

		while (remaining)
		{
			__asm __volatile__
			(
				"move.b %1,(%0)+;"
				: "+a"(dst8)
				: "r"(zero)
				:
			);

			remaining--;
		}

		dst = dst8;
	}
}

// check if key event is waiting
static FORCEINLINE bool AGT_isKey()
{
	return (key_eventtail != key_eventhead);
}

static FORCEINLINE void AGT_debouncedKeyClearAll()
{
	clear<256>(debounced_key_states);
}

// debounced check if key release occurred since last test
static FORCEINLINE u8 AGT_debouncedKeyPressed(s16 _scancode)
{
	return (debounced_key_states[_scancode]);
}

// debounced check if key release occurred since last test
static FORCEINLINE u8 AGT_debouncedKeyReleased(s16 _scancode)
{
	return (debounced_key_states[_scancode + s16(128)]);
}

// debounced check if key press occurred since last test
static FORCEINLINE u8 AGT_debouncedKeyPressedClear(s16 _scancode)
{
	u8 &ref = debounced_key_states[s16(_scancode)];
	u8 s = ref;
	ref = 0;
	return  s;
}

// debounced check if key release occurred since last test
static FORCEINLINE u8 AGT_debouncedKeyReleasedClear(s16 _scancode)
{
	u8 &ref = debounced_key_states[s16(_scancode + s16(128))];
	u8 s = ref;
	ref = 0;
	return  s;

//	debounced_key_states[s16(_scancode + s16(128))] = 0;
}

// pop waiting key (returns scancode: bit 7 = release/press)
static FORCEINLINE u8 AGT_popKey()
{
	u8 kcode = 0;
	s16 tail = key_eventtail;
	if (tail != key_eventhead)
	{
		kcode = (u8)(key_eventring[tail]);
		tail = (tail + 1) & 255;
	}
	key_eventtail = tail;

	return kcode;
}

// check if mousebutton event is waiting
static FORCEINLINE bool AGT_isMouseClick()
{
	return (msclick_eventtail != msclick_eventhead);
}

// pop waiting mousebutton
static FORCEINLINE u8 AGT_popMouseClick()
{
	u8 kcode = 0;
	s16 tail = msclick_eventtail;
	if (tail != msclick_eventhead)
	{
		kcode = (u8)(msclick_eventring[tail]);
		tail = (tail + 1) & 255;
	}
	msclick_eventtail = tail;

	return kcode;
}

// check if joy0 event is waiting
static FORCEINLINE bool AGT_isJoy0()
{
	return (joy0_eventtail != joy0_eventhead);
}

// pop waiting joy0 event
static FORCEINLINE u8 AGT_popJoy0()
{
	u8 kcode = 0;
	s16 tail = joy0_eventtail;
	if (tail != joy0_eventhead)
	{
		kcode = (u8)(joy0_eventring[tail]);
		tail = (tail + 1) & 255;
	}
	joy0_eventtail = tail;

	return kcode;
}

// check if joy1 event is waiting
static FORCEINLINE bool AGT_isJoy1()
{
	return (joy1_eventtail != joy1_eventhead);
}

// pop waiting joy1 event
static FORCEINLINE u8 AGT_popJoy1()
{
	u8 kcode = 0;
	s16 tail = joy1_eventtail;
	if (tail != joy1_eventhead)
	{
		kcode = (u8)(joy1_eventring[tail]);
		tail = (tail + 1) & 255;
	}
	joy1_eventtail = tail;

	return kcode;
}

// ---------------------------------------------------------------------------------------------------------------------
// install keyboard & joystick services

extern "C" int AGT_InstallInputService();

// ---------------------------------------------------------------------------------------------------------------------
// remove keyboard & joystick services (note: keyboard & mouse will then be off! shutdown will restore things on exit)

extern "C" int AGT_RemoveInputService();

// ---------------------------------------------------------------------------------------------------------------------

#endif // input_h_
