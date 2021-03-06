#ifndef INCLUDED_GUI_WIFISELECTOR_H
#define INCLUDED_GUI_WIFISELECTOR_H

#include "selector.h"
#include "../textBits.h"
#include "textArea.h"

struct wifi_choice
{
	int index;
	char name[64];
};


class wifiSelector : public selector{
public:
	wifiSelector(const unsigned int &targetX, const unsigned int &targetY, const string &image);
	~wifiSelector();
	
	//From guibit:
	void draw();
	
	//From inputable:
	string getInputKey() const;
protected:
	//functions called when things happen in takeInput,
	//the value returned here is the value returned to takeInput
	virtual string fixSelected();
	virtual string pressCross();
	virtual string pressCircle();
	virtual string pressSelect();

	textBlock infoText;
	textArea* text;
	
	textBlock logText;
	textArea* text2;
	
	wifi_choice choices[10];
	int currentChoice;
	int pick_count;
};

#endif //INCLUDED_GUI_CHATSELECTOR_H
