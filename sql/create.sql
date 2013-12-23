CREATE TABLE GameInfo(
Version integer primary key
);

CREATE TABLE SetInfo(
ID integer primary key,
Name text
);

CREATE TABLE CardSet(
ID integer primary key,
SetID integer,
CardID integer,
Rarity integer
);

CREATE TABLE CardRule(
ID integer primary key,
CardID integer,
Data text,
Hint text
);

CREATE TABLE BasicCard(
ID integer primary key,
Name text,
Cost text,
Pwr integer,
Tgh integer,
TypeSuper text,
TypeCard text,
TypeSub text
);

CREATE TABLE Library(
ID integer primary key,
CardID integer
);

CREATE TABLE Deck(
ID integer primary key,
DeckID integer,
CardID integer
);

CREATE TABLE DeckInfo(
ID integer primary key,
Name text
);
