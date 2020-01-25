# Description
This is a client-server implementation of the Connect Four board game.

# Usage
## Compilation
```
make
```

## Running the server
```
./server [-p PORT] [-w WIDTH] [-h HEIGHT]
```
- PORT - number of the port on which the server will run (default: 8051)
- WIDTH - width of the game board (default: 7)
- HEIGHT - height of the game board (default: 6)

## Running the client
```
./client HOST[:PORT] NAME
```
- HOST - server ip
- PORT - server port (default: 8051)
- NAME - name with which the client will log in to the server

## Client controls
### In menus
- UP, DOWN - move between the menu items
- ENTER - select the current item
### In game
- LEFT, RIGHT - move selection between columns
- ENTER - drop the disc to the currently selected column
- u - undo the last action
- ESCAPE - quit the game
