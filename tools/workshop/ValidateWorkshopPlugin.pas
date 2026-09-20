unit ValidateWorkshopPlugin;

var
  Errors: TStringList;
  CheckedRecords: Integer;

procedure CheckTree(e: IInterface);
var
  i: Integer;
  MessageText: string;
begin
  MessageText := Check(e);
  if MessageText <> '' then
    Errors.Add(FullPath(e) + ': ' + MessageText);
  for i := 0 to Pred(ElementCount(e)) do
    CheckTree(ElementByIndex(e, i));
end;

function Initialize: Integer;
var
  Plugin, Group, Item: IInterface;
  i, j: Integer;
  Lines: TStringList;
begin
  Result := 1;
  Errors := TStringList.Create;
  Lines := TStringList.Create;
  CheckedRecords := 0;
  try
    try
      Plugin := FileByName('Realistic Reflections - Mirrors.esm');
      if not Assigned(Plugin) then
        raise Exception.Create('Mirror ESM was not loaded');
      if MasterCount(Plugin) <> 1 then
        raise Exception.Create('Unexpected master count');
      for i := 0 to Pred(ElementCount(Plugin)) do begin
        Group := ElementByIndex(Plugin, i);
        CheckTree(Group);
        if ElementType(Group) = etGroupRecord then begin
          for j := 0 to Pred(ElementCount(Group)) do begin
            Item := ElementByIndex(Group, j);
            if (Signature(Item) = 'STAT') or (Signature(Item) = 'COBJ') or (Signature(Item) = 'KYWD') or
               (Signature(Item) = 'TXST') or (Signature(Item) = 'MSWP') then begin
              Inc(CheckedRecords);
              Lines.Add(FullPath(Item));
            end else
              Errors.Add('Unexpected record: ' + FullPath(Item));
          end;
        end;
      end;
      if CheckedRecords <> 32 then
        Errors.Add('Expected sixteen STAT records, thirteen COBJ recipes, the Mirrors category, TXST and MSWP');
      if Errors.Count <> 0 then begin
        Lines.Insert(0, 'FAIL: xEdit record validation');
        Lines.AddStrings(Errors);
      end else
        Lines.Insert(0, 'PASS: xEdit checked all thirty-two records and resolved all form links');
    except
      on E: Exception do Lines.Insert(0, 'FAIL: ' + E.Message);
    end;
    Lines.SaveToFile(DataPath + 'result.txt');
  finally
    Lines.Free;
    Errors.Free;
  end;
end;

end.
