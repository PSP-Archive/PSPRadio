#include "chatSelector.h"
#include "../bitlbee.h"
#include "../dlib/render.h"
#include <iostream>
#include "../dlib/util.h"

chatSelector::chatSelector(const unsigned int &targetX, const unsigned int &targetY, const string &image)
{
	init(targetX, targetY, 10, -1, image);
	
	contactsList = new textBlock(15, 1000);//1000 to keep all buddies
	contactsListA = new textArea(15, CHAT_AREA_HEIGHT, contactsList);
	contactsListA->moveTo(382,6);
	contactsListA->setAlign(ALIGN_TOP);
//	addGuiBit(contactsListA); ??

	selectedNick = wstring();
	contactListOffset = 0;

}

chatSelector::~chatSelector()
{
	delete contactsListA;
	delete contactsList;
}

string chatSelector::getInputKey() const
{
	return "chatSelector";
}

string chatSelector::fixSelected()
{
	//wrap in either direction, -1 is for server window
	if (selected < -1)
		selected += bitlbeeCallback::getBee()->chatContacts.size() + 1;
	if (selected >= (int)contacts->size())
		selected = -1;

	//change it in bitlbee
	cout << "old nick:" << unUnicode(selectedNick ) << endl;
	selectedNick = bitlbeeCallback::getBee()->changeChatToInt(selected);
	cout << "new nick:" << unUnicode(selectedNick ) << endl;
	notifyContactChange(); //HACK, see below
	findNewScrollOffset();
	
	return getInputKey();
}

/*
	Clarification of above:
	When changeChatToInt is called, we receive a notifyContactChange() callback from bitlbeeCallback if the user had an unread message.
	however, we do not know the new selectedNick untill after changeChatToInt has returned, so we just waste time in the callback.
	Therefore we must call notifiyContactChange() ourself so that the newly selected contact can be selected in the list.
*/

string chatSelector::pressCross()
{
	cout << "X" << endl;
	//change selected item to chat box
	return "chatInput";
}

string chatSelector::pressCircle()
{
	return getInputKey();
}

string chatSelector::pressSelect()
{
	//change selected item to menu?
	return "menuMain";
}

void chatSelector::draw()
{
	//draw even if we aren't active
	
	dirty = false;
	
	
	if (selected >= 0) 
	{
		//update offset
		offset = targetOffset;
		offset.y += (selected - contactListOffset) * itemSize;
		//draw
		SDL_BlitSurface(pixels, &rect, screen, &offset);
	}
	contactsListA->draw();
}

void chatSelector::notifyContactChange()
{
	dirty = true;
	
	selected = -1;	//set selected to server
	
	//TODO REDO HACK TODO
	cout << "redoContactsBox()" << endl;
	contactsListA->clearArea();
	contactsList->reset();
	list< bitlbeeUser >::const_iterator iter;
	int chatIndex = 0;
	for (iter = contacts->begin(); iter != contacts->end(); iter++)
	{
		if ((*iter).nick == selectedNick) //this is the person, set the selected value
			selected = chatIndex;
		
		unsigned int nameColor;
		if ((*iter).status == BU_ONLINE)
			nameColor = COLOR_USER_ONLINE;
		else if ((*iter).status == BU_AWAY)
			nameColor = COLOR_USER_AWAY;
		else //offline
			nameColor = COLOR_USER_OFFLINE;
		if ((*iter).unreadMessages)
		{
			nameColor = COLOR_USER_UNREAD;
		}
		contactsList->addText((*iter).nick.substr(0,15) + (wchar_t)'\n', nameColor);
		chatIndex++;
	}
	
	if (selected == -1)
		selectedNick = wstring();
	
	findNewScrollOffset();
}

//Called when someone is renamed on your contact list (after altering the contact list list)
void chatSelector::notifyContactRename(wstring oldNick, wstring newNick)
{
	if (selectedNick == oldNick)
	{
		selectedNick = newNick;
	}
	notifyContactChange();
}

//updates the contactListOffset variable and scrolls the contactsListA
void chatSelector::findNewScrollOffset()
{
	if (contacts->size() > CHAT_AREA_HEIGHT)
	{
		int diff = contacts->size() - CHAT_AREA_HEIGHT;
		if (selected > CHAT_SCROLL_DEPTH)
		{
			contactListOffset = selected - CHAT_SCROLL_DEPTH;
			if (contactListOffset > diff) 
			{
				contactListOffset = diff;
			}
		}
		else
		{
			contactListOffset = 0;
		}
	}
	else
	{
		contactListOffset = 0;
	}
	contactsListA->setLinesToSkip(contactListOffset);
}

void chatSelector::setContactList(list < bitlbeeUser > *newContacts)
{
	contacts = newContacts;
}
