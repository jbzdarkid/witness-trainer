#pragma once

enum SubWeapon {
	LandMine = 0x00,
	Grenade = 0x01,
	Boomerang = 0x02,
	Tennis = 0x03,
	Fireball = 0x04,
	Ice = 0x05,
	Fan = 0x06,
	Vacuum = 0x07,
	Dart = 0x08,
	EnergyBall = 0x09,
	Airplane = 0x0A,
	Frog = 0x0B,
	Poison = 0x0C,
};

struct SaveData {
	byte headShape;
	byte headId;
	SubWeapon subA;
	SubWeapon subB;

	int gems;
	int yarn;
};