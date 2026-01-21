
#include <stdint.h>
#include <stdlib.h>
#include <stddef.h>


template<typename T>
struct TypeInfo { };

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

template<typename T>
struct NameRegistryItem : public NameRegistryItemBase
{
public:
    NameRegistryItem(const char* name, const T& data) :
        NameRegistryItemBase{ name, &data, sizeof(T) }
    {
        registerNameItem(this);
    }
};

struct StateFieldInfo
{
    uint16_t pos;
    uint8_t bits;
    uint8_t typeId;
};


template<typename T>
class Exported {
private:

    StateFieldInfo info;
    NameRegistryItem<StateFieldInfo> registryItem;
	T value;

public:
	Exported(const char* name, uint8_t pos, T initialValue, int bits = -1) :
		value(initialValue),
		pos(pos)
	{
		addStateField<T>(info);
        registryItem.register(name, info);
	}

    operator T() {
        return value;
    }

    void operator=(T x) {
        if (value != x) {
            value = x;
            TypeInfo<T>::setState(pos, value, bits);
        }
    }

};


template<typename T>
class Imported {
private:
	uint8_t nodeAddress;
	uint8_t pos;
	int8_t timer;
	bool valid;
	bool ready;
	T value;
public:
	Imported(uint8_t node, uint8_t id, T initialValue) :
		value(initialValue),
		id(id),
		timer(120),
		valid(false),
		ready(false)
	{
		registerUpdateCallback(updateCallback, this);
		registerRecvCallback(node, recvCallback, this);
	}

    operator T() {
        return value;
    }

    void operator=(T x) {
        value = x;
    }

private:
	static void updateCallback(void* context, uint8_t ticks)
	{
		static_cast<Imported<T>*>(context)->update(ticks);
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

	static bool recvCallback(void* context, uint8_t* data, int size)
	{
		if (size < sizeof(uint8_t) + sizeof(T)) {
			return false;
		}
		uint8_t id = data[0];
		T value;
		memcpy(&value, &data[1], sizeof(T));
		return static_cast<Imported<T>*>(context)->recv(id, value);
	}

	bool recv(uint8_t _id, T _value)
	{
		if (_id != id) {
			return false;
		}
		value = _value;
		timer = 120;
		valid = true;
		ready = true;
		return true;
	}
};
