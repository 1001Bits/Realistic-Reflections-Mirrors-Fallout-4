unit ConvertWorkshopNifs;

function Initialize: Integer;
var
  Nif, Reopened: TwbNifFile;
  Names, ResultLines: TStringList;
  i, j: Integer;
  Block: TwbNifBlock;
  FileName: string;
begin
  Result := 1;
  dfFloatDecimalDigits := 9;
  Names := TStringList.Create;
  ResultLines := TStringList.Create;
  Nif := TwbNifFile.Create;
  Reopened := TwbNifFile.Create;
  try
    try
      Names.LoadFromFile(DataPath + 'nif-inputs.txt');
      for i := 0 to Pred(Names.Count) do begin
        AddMessage('Converting: ' + Names[i]);
        Nif.LoadFromJsonFile(DataPath + 'Input\' + Names[i]);
        for j := 0 to Pred(Nif.BlocksCount) do begin
          Block := Nif.Blocks[j];
          if Block.BlockType = 'BSTriShape' then
            Block.UpdateBounds;
        end;
        FileName := DataPath + 'Output\' + ChangeFileExt(Names[i], '.nif');
        Nif.SaveToFile(FileName);
        Reopened.LoadFromFile(FileName);
        if Reopened.BlocksCount <> Nif.BlocksCount then
          raise Exception.Create('Block count changed in ' + Names[i]);
        Reopened.SaveToJsonFile(DataPath + 'Output\' + Names[i], False);
      end;
      ResultLines.Add('PASS: saved and independently reopened all workshop NIFs');
    except
      on E: Exception do ResultLines.Add('FAIL: ' + E.Message);
    end;
    ResultLines.SaveToFile(DataPath + 'result.txt');
  finally
    Reopened.Free;
    Nif.Free;
    ResultLines.Free;
    Names.Free;
  end;
end;

end.
