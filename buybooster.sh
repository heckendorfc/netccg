#!/bin/sh

LIST=""

for x in "11-1" "3-2" "1-3,4"
do
	N=`echo $x | sed 's/-.*//'`
	R=`echo $x | sed 's/.*-//'`
	A=`echo "SELECT ID FROM BasicCard WHERE ID IN (SELECT CardID FROM CardSet WHERE SetID IN (SELECT ID FROM SetInfo WHERE Name=\"$2\") AND Rarity IN ($R)) ORDER BY random() LIMIT $N;" | sqlite3 $1`
	LIST="$LIST $A"
done

CL=`echo $LIST | sed 's/ /,/g'`
echo "You opened one or more of:"
echo "SELECT Name,Rarity FROM BasicCard JOIN CardSet ON BasicCard.ID=CardSet.CardID WHERE CardSet.SetID=(SELECT ID FROM SetInfo WHERE Name=\"$2\") AND BasicCard.ID IN ($CL);" | sqlite3 -header $1

CL=`echo $LIST | sed 's/ /),(/g'`
echo "Inserting into library..."
echo "INSERT INTO Library (CardID) VALUES($CL);" | sqlite3 $1
echo "Done."
