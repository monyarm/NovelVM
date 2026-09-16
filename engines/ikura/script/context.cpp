#include "ikura/script/context.h"

namespace Ikura::VM {

int32 Context::decodeValue(uint32 raw) const {
	if (raw & 0x80000000u)
		raw = (uint32)getValue(raw & 0x7FFFFFFFu);
	if (raw & 0x40000000u)
		raw |= 0x80000000u;
	return (int32)raw;
}

int32 Context::getValue(uint32 position) const {
	uint32 value = position < _variables.size() ? _variables[position] : 0;
	if (value & 0x40000000u)
		value |= 0x80000000u;
	return (int32)value;
}

void Context::setValue(uint32 position, uint32 raw) {
	if (position >= _variables.size())
		_variables.resize(position + 1, (uint32)0);
	_variables[position] = raw & ~0x80000000u;
}

bool Context::getFlag(uint32 position) const {
	return position < _flags.size() && _flags[position];
}

void Context::setFlag(uint32 position, bool value) {
	if (position >= _flags.size())
		_flags.resize(position + 1, false);
	_flags[position] = value;
}

bool Context::getSystem(uint32 position) const {
	return position < _system.size() && _system[position];
}

void Context::setSystem(uint32 position, bool value) {
	if (position >= _system.size())
		_system.resize(position + 1, false);
	_system[position] = value;
}

void Context::jumpScript(Format::Script::Script *script) {
	delete _script;
	_script = script;
}

void Context::callScript(Format::Script::Script *script) {
	_scriptStack.push_back(_script);
	_script = script;
}

bool Context::returnScript() {
	if (_scriptStack.empty())
		return false;
	delete _script;
	_script = _scriptStack.back();
	_scriptStack.pop_back();
	return true;
}

} // End of namespace Ikura::VM
