# Megathon 2k25

## Problem Statement
  
### Minimum Requirements:

Make a rich text editor.
- It should allow multiple people to edit at the same time.

- It should only allow authorized people to read and write.

- It should show highlights and cursors of all users, live.

### Recommended:

- It should support executable code blocks (executing code within the document)

- It should support multimedia (images, videos, etc.)

- It should have AI completions.

- It should have chat with other users (and with AI).

- It should store version history of the document over time, and allow reverting to previous versions.

  
Anything else you can come up with.

The project has to be in C, to keep it fair for everyone.

Conflicts should be handled **appropriately** (hint: maybe use locks). - *no locks if you find a better way :) *

Offline editing and corresponding merge conflicts handling should be implemented too.

It should have a status bar with basic statistics.
  
Who's doing what

**Arnav** : On charge of authentication. 
- auth.c
	- Has the methods signup, login, authenticate, encrypt, decrypt
**Navneet** : Researching on conflict resolution, how text buffer works etc. 
**Navya** : Working on UI.
**Dan** : Working on websocket implementation.
- sockets.c 
	- Has whatever methods Dan can think of 