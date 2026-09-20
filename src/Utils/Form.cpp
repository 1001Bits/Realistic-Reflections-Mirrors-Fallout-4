#include "Form.h"

auto Util::GetFormFromIdentifier(const std::string& a_identifier) -> RE::TESForm*
{
	std::istringstream ss{ a_identifier };
	std::string plugin, id;

	std::getline(ss, plugin, '|');
	std::getline(ss, id);
	RE::FormID relativeID;
	std::istringstream{ id } >> std::hex >> relativeID;
	const auto dataHandler = RE::TESDataHandler::GetSingleton();
	return dataHandler ? dataHandler->LookupForm(relativeID, plugin) : nullptr;
}

auto Util::GetIdentifierFromForm(const RE::TESForm* a_form) -> std::string
{
	if (a_form == nullptr) {
		return "0|Null";
	}
	auto* mutableForm = const_cast<RE::TESForm*>(a_form);
	if (auto file = a_form->GetFile()) {
		return std::format("{:X}|{}", mutableForm->GetLocalFormID(), file->GetFilename());
	}
	return std::format("{:X}|Generated", mutableForm->GetLocalFormID());
}
