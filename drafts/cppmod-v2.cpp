
#include <stdint.h>
#include <stdlib.h>
#include <stddef.h>

extern "C" 
{
	// Imports (pointers in Flash at known location)
	bool (*const configureOutput)(int port, int type, bool initialValue);
	void (*const setOutput)(int port, bool value);
	void (*const setRelay)(int port, bool value);
	bool (*const getInput)(int port);
	bool (*const send)(void* data, int length);
	void (*const setLEDPower)(int port, int powerUA);
	uint8_t (*const getMyAddress)();

	// Exports (pointers at the beginning of application segment)
	void _start(); // Calls constructors and main()
	void _stop(); // Calls destructors
	void recvCallback(uint8_t srcAddress, uint8_t* data, int size);
	void updateCallback(uint32_t ticks);
}

template<typename T>
struct TypeInfo { };

enum {
	OBJECT_KIND_DATA = 0,
	OBJECT_KIND_SIGNAL = 1,
};

enum {
	UPDATE_NEXT_TIME = 0,
	UPDATE_NOW = 1,
	UPDATE_WITH_REPEAT = 3,
};

template<>
struct TypeInfo<bool> {
    static constexpr uint8_t id = 1;
    static constexpr uint8_t bits = 1;
    static void setState(size_t pos, bool value, size_t bits);
};


struct NameRegistryItemBase
{
    struct NameRegistryItemBase* next = nullptr;
    const char* name;
    const void* data;
    const size_t size;
};

struct NameRegistryItem : public NameRegistryItemBase
{
public:
template<typename T>
    NameRegistryItem(const char* name, const T& data) :
        NameRegistryItemBase{ name, &data, sizeof(T) }
    {
        registerNameItem(this);
    }
};

struct StateEntryInfo
{
	uint16_t location;
	uint8_t bits;
    uint8_t typeId;
};

uint16_t stateBits = 0;


template<typename T>
class Exported {
private:

	T value;
	StateEntryInfo info;
    NameRegistryItem registryItem;

public:
	Exported(const char* name, T initialValue, int bits = -1) :
		value(initialValue),
		info{stateBits, bits < 0 ? TypeInfo<T>::bits : bits, typeId}
	{
        TypeInfo<T>::setState(info.location, info.bits, value);
        registryItem.register(name, OBJECT_KIND_DATA, info);
	}

    operator T() {
        return value;
    }

    void operator=(T x) {
        if (value != x) {
            value = x;
            TypeInfo<T>::setState(info.location, info.bits, value);
			updateState(UPDATE_NOW);
        }
    }
};


template<typename T>
class Imported {
private:
	int8_t timer;
	bool valid;
	bool ready;
	T value;

    NameRegistryImport registry;
	StateEntryInfo info;

public:
	Imported(const char* name, T initialValue) :
		value(initialValue),
		id(id),
		timer(120),
		valid(false),
		ready(false)
	{
		registry.register(name, info);
		registerUpdateCallback(this, Imported::update);
		registerStateUpdateCallback(this, Imported::update);
	}

    operator T() {
        return value;
    }

private:

	void updateState(const IncomingState& state)
	{
		if (registry.valid && state.address == registry.address) {
			value = TypeInfo<T>::getField(state, info.location, info.bits);
			valid = true;
			timer = 120;
		}
	}

	void update(uint8_t ticks)
	{
		timer -= ticks;
		if (timer <= 0) {
			timer = 120;
			valid = false;
			ready = true;
		}
	}
};

Exported<bool> lightSwExported("light-switch", false);
DigitalInput lightSw(4);

Imported<bool> remoteSw("room-some-switch", false);
DigitalOutput lightOutput(2);
Exported<bool> lightOutputExported("light-output", false);

Signal<bool> cloudSwitch("room-cloud-switch");

template<typename Tin, typename Tout>
void normalLight(Tin& input, Tout& output, Signal<bool>& cloudSignal)
{
	if (input.changed()) {
		output = input.get();
	} else if (cloudSignal.triggered()) {
		output = cloudSignal.get();
	}
}

template<typename Tin1, typename Tin2, typename Tout>
void stairsLight(Tin1& input1, Tin2& input2, Tout& output, Signal<bool>& cloudSignal)
{
	if (input1.changed() || input2.changed()) {
		output = boolXOR(input1.get(), input2.get());
	} else if (cloudSignal.triggered()) {
		output = cloudSignal.get();
	}
}

void update(uint32_t ticks)
{
	stairsLight(lightSw, remoteSw, lightOutput, cloudSwitch);
	lightSwExported = lightSw;
	lightOutputExported = lightOutput;
}