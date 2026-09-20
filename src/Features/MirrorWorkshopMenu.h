#pragma once

namespace MirrorWorkshopMenu
{
	template <class List, class Form>
	bool AppendOnce(List* menu, Form* category)
	{
		if (!menu || !category)
			return false;
		if (!menu->GetItemIndex(*category))
			menu->arrayOfForms.push_back(category);
		return true;
	}
}
