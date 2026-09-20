unit ValidateAuthoring;
var Errors, Lines: TStringList; Checks: Integer;

procedure Require(OK: Boolean; MessageText: string);
begin
  Inc(Checks);
  if not OK then Errors.Add(MessageText);
end;

procedure CheckTree(e: IInterface);
var i: Integer; MessageText: string;
begin
  Inc(Checks);
  MessageText := Check(e);
  if MessageText <> '' then Errors.Add(FullPath(e) + ': ' + MessageText);
  for i := 0 to Pred(ElementCount(e)) do CheckTree(ElementByIndex(e, i));
end;

function Initialize: Integer;
var Plugin, Group, Item, Assignment: IInterface; i,j,Forms: Integer; Material: TwbBGSMFile;
begin
  Result := 1; Checks := 0; Forms := 0;
  Errors := TStringList.Create; Lines := TStringList.Create; Material := TwbBGSMFile.Create;
  try
    try
      Plugin := FileByName('Realistic Reflections - Mirrors.esm');
      Require(Assigned(Plugin), 'Shared mirror ESP missing');
      Require(MasterCount(Plugin) = 1, 'Only Fallout4.esm may be a master');
      for i := 0 to Pred(ElementCount(Plugin)) do begin
        Group := ElementByIndex(Plugin,i); CheckTree(Group);
        if ElementType(Group) = etGroupRecord then
          for j := 0 to Pred(ElementCount(Group)) do begin
            Item := ElementByIndex(Group,j); Inc(Forms);
            if Signature(Item) = 'TXST' then begin
              Require((GetLoadOrderFormID(Item) and $FFFFFF) = $B00, 'Texture set local form ID');
              Require(EditorID(Item) = 'MOF_MirrorSurface', 'Texture set Editor ID');
              Require(GetElementEditValues(Item,'MNAM') = 'MirrorsOfFallout\Authoring\MOF_MirrorSurface.bgsm', 'TXST material path');
            end else if Signature(Item) = 'MSWP' then begin
              Require((GetLoadOrderFormID(Item) and $FFFFFF) = $B01, 'Swap template local form ID');
              Require(EditorID(Item) = 'MOF_MirrorSurfaceSwapTemplate', 'Swap template Editor ID');
              Require(GetElementEditValues(Item,'Material Substitutions\[0]\SNAM') = 'MirrorsOfFallout\Authoring\MOF_MirrorSurface.bgsm', 'Swap replacement material');
            end else if (Signature(Item) <> 'STAT') and (Signature(Item) <> 'COBJ') and
                        (Signature(Item) <> 'KYWD') then Errors.Add('Unexpected form: '+Signature(Item));
          end;
      end;
      Require(Forms = 32, 'Thirty workshop forms and two authoring forms');
      Plugin := FileByName('MirrorsOfFallout-AuthoringTest.esp');
      if Assigned(Plugin) then begin
        Require(MasterCount(Plugin) = 2, 'Test plugin requires Fallout4.esm and shared mirror ESP');
        Forms := 0;
        for i := 0 to Pred(ElementCount(Plugin)) do begin
          Group := ElementByIndex(Plugin,i); CheckTree(Group);
          if ElementType(Group) = etGroupRecord then
            for j := 0 to Pred(ElementCount(Group)) do begin
              Item := ElementByIndex(Group,j); Inc(Forms);
              Require(Signature(Item) = 'STAT', 'Test plugin contains only new statics');
              Assignment := ElementByPath(Item,'Model\MODS');
              if EditorID(Item) = 'MOFCKTestUntagged' then Require(not Assigned(Assignment),'Negative control must have no swap')
              else Require(EditorID(LinksTo(Assignment)) = 'MOF_MirrorSurfaceSwapTemplate','Test swap resolves to master template');
            end;
        end;
        Require(Forms = 4, 'Four test statics');
      end;
      Material.LoadFromFile(DataPath+'Materials\MirrorsOfFallout\Authoring\MOF_MirrorSurface.bgsm');
      Material.SaveToFile(DataPath+'material-reopened.bgsm');
      if Errors.Count = 0 then Lines.Add('PASS: '+IntToStr(Checks)+' authoring record checks and BGSM parse/round-trip')
      else Lines.Add('FAIL: '+Errors.Text);
    except on E: Exception do Lines.Add('FAIL: '+E.Message); end;
    Lines.SaveToFile(DataPath+'result.txt');
  finally Material.Free; Lines.Free; Errors.Free; end;
end;
end.
