
#include <stdint.h>
#include <string.h>

// TODO: Register callbacks should take one more data structure for linked list and storage
void registerUpdateCallback(void (*callback)(void* context, uint8_t ticks), void* ctx);
void registerAfterUpdateCallback(void (*callback)(void* context, uint8_t ticks), void* ctx);
void registerRecvCallback(uint8_t node, bool (*callback)(void* context, uint8_t* data, int size), void* ctx);

extern uint8_t tmpBuffer[128];

bool broadcastRaw(void* data, int length, bool global);
extern int randomDelay();

template<typename T1, typename T2>
bool broadcast(T1 a, T2 b, bool global)
{
    memcpy(tmpBuffer, &a, sizeof(a));
    memcpy(&tmpBuffer[sizeof(a)], &b, sizeof(b));
    return broadcastRaw(tmpBuffer, sizeof(a) + sizeof(b), global);
}

template<typename T>
class Exported {
private:
	T value;
	T old;
	uint8_t id;
	int8_t timer;
    bool global;

public:
	Exported(uint8_t id, T initialValue, bool global = false) :
		value(initialValue),
		old(initialValue),
		id(id),
		timer(0),
        global(global)
	{
		registerUpdateCallback(updateCallback, this);
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
		static_cast<Exported<T>*>(context)->update(ticks);
	}

	void update(uint8_t ticks)
	{
		timer -= ticks;
		if (old != value || timer <= 0) {
			if (broadcast(id, value, global)) {
                timer = 35 + randomDelay() % 10;
            } else {
                timer = 0;
            }
            old = value;
		}
	}

};

template<typename T>
class Imported {
private:
	uint8_t id;
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

extern void ioWrite(uint8_t port, bool value);
extern bool ioRead(uint8_t port);

class DigitalInput
{
private:
    uint8_t port;
public:
    DigitalInput(uint8_t port): port(port){}
    operator bool() { return ioRead(port); }
    void operator=(bool x) { ioWrite(port, x); }
};


class Event
{
private:
    uint8_t id;
    void (*callback)();
public:
    Event(uint8_t id, void (*callback)()) : id(id), callback(callback)
    {

		registerRecvCallback(0xFF, recvCallback, this);
    }

private:
	static bool recvCallback(void* context, uint8_t* data, int size)
	{
		return static_cast<Event*>(context)->recv(data[0]);
	}

	bool recv(uint8_t _id)
	{
		if (_id != id) {
			return false;
		}
        if (!broadcast(id, 0/* TODO: send to triggering node */, true)) {
            return true; // to simulate lost input packet (do not call callback)
        }
        callback();
		return true;
	}
};

class EventTrigger
{
private:
    uint8_t node;
    uint8_t id;
    int8_t timer;
    bool active;
    bool retry;
    void (*callback)(void* ctx, bool success);
    void* ctx;
public:
    bool trigger() {
        if (active) {
            false;
        }
        active = true;
        if (!broadcast(id, node, true)) {// TODO global
            retry = true;
        }
        return true;
    }
    // on recv: callback with success when ack arrived
    // on update: retry if needed (if retry success, reset timer)
    //            callback(false) if timeout
};

/*
TODO1:
  Configuration script should create identifiers for nodes and ids.
  If should also generate macro that with provided list of target nodes,
  it should return if it is global or not. Or, better, based on
  configuration should create entire Export/Import snippet for each module.
TODO2:
  To save program memory, template classes should inherit from non-template
  classes. All common operations (as much as possible) should be in the
  non-template part.
TODO3:
  Event can be a template with callback parameters as template arguments.
TODO4:
  Gate module must inform local modules that it is not able to pass
  any more packets to global network. If local queue is also full
  broadcast will return false. Other way should be always possible
  since to local bus is faster than global.
TODO5:
  The code should be compiled to a binary blob with predefied C API.
  It will be put into special section of the flash.
*/

void eventCall();

Exported<bool> ex(0, 12, false);
Imported<bool> im(4, 0, 12);
Imported<int8_t> im2(3, 0, 12);
DigitalInput input(10);
Event e(10, eventCall);

void eventCall() {
    ex = 33;
}


void f() {
    extern void g(bool);
    g(ex);
    ex = im + im2;
    g(ex);
    ex = input;
}
