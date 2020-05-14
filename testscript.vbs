'	This program illustrates the use of a simple script to control the RoboFocus  
'	switches to control the automation. Program assumes that the RF application has  
'	not been opened yet. 
'	Program Flow 
'	Starts Instruments 
'	Open Dome 
'	Note: 
' ******************************************************************************** 
'Global Objects 
Dim ar         'ascom Remote 1
Dim u				'Requires ASCOM development platform  - helper class 
' ******************************************************************************** 
'Global variables 
Dim Startup  
' ******************************************************************************** 
' * 
' * Main Program Flow 
' * 
Call Welcome 
Call createObjects
Call connectObjects 
Call testDriverinfo
Call testActions
Call testMethods
Call disconnectObjects
Call disposeObjects
 
wscript.echo "Startup Successful at", Time 
WScript.Quit 
 
' ******************************************************************************** 
' * 
' * Subroutines  
' * 
'******************************************************************************** 
Sub Welcome() 
	Startup = MsgBox ("Initialize ASCOM switch testing", 4, "Confirm test") 
	' Startup contains either 6 or 7, depending on which button is clicked. 
	If Startup = 7 then WScript.Quit 
	WScript.Echo "Push OK to begin Startup" 
End Sub 
'******************************************************************************** 
function OpenPrgs( progname ) 
	Set oWmi = GetObject("winmgmts:" & " {impersonationLevel=impersonate }!    " & "." & "  root  cimv2") 
 
	TimeEnd = Timer + 2 
	Do While Timer < TimeEnd 
	Loop 
	query = "Select * from Win32_Process Where Name = '"
   query = query + progname
   query = query + "'"
   Set colProcessList = oWmi.ExecQuery(query) 
	if colProcessList.count = 0 then 
		Set WshShell = CreateObject("WScript.Shell") 
		set oExec = WshShell.Exec("C:  Program Files  Common Files  ASCOM  Focuser  RoboFocus.exe") 
		TimeEnd = Timer + 10 
		Do While Timer < TimeEnd 
		Loop 
	end if 
 
End func 
'******************************************************************************** 
Sub createObjects() 
	Set ar = WScript.CreateObject("ASCOM.Remote1.Switch") 
'	Set u = CreateObject("DriverHelper.Util") 
End Sub 
'******************************************************************************** 
Sub connectObjects() 
	ar.connected = true
   If ar.connected Then 
		wscript.echo "ASCOM remote switch connected." 
   else 
      wscript.echo "ASCOM remote switch failed to connect." 
      WScript.Quit 
	End If 
End Sub 

'******************************************************************************** 
Sub disconnectObjects() 
	ar.connected = false
   If ar.connected Then 
		wscript.echo "ASCOM remote switch failed to disconnect." 
	End If 
End Sub 

'******************************************************************************** 
Sub disposeObjects() 
	set ar = Nothing 
	Set u = Nothing 
End Sub  
'******************************************************************************** 
Sub testMethods() 
  Dim i = 0
  for i=0, ar.MaxSwitch step 1
   if ar.CanWrite( i ) then
      wscript.echo "Switch component " & i & ar.getSwitchDescription( i )
      wscript.echo "Switch name " & i & ar.getSwitchName( i )
      wscript.echo "Switch component " & i &  ar.getSwitchDescription( i )
      wscript.echo "Switch step size " &  i & ar.SwitchStep( i )
      if ar.switchStep = 1.0 then
        ar.setSwitch( i, true )
        delay(1000)
        ar.setSwitch( i, false )
        delay(1000)
      else
         wscript.echo "Non-boolean switch found - can't toggle: " & i
      end if 
   end if 
  next 
End Sub 
'******************************************************************************** 
Sub testDriverInfo() 
'	Check all the basic driver information first 
	if  ar.Connected = false then  
      ar.Connected = true
	wscript.echo "Driver name is " & ar.Name 
   wscript.echo "Driver version is " & ar.DriverVersion
   wscript.echo "API interface version is" & ar.InterfaceVersion
   wscript.echo "Driver Info is ", ar.DriverInfo
   wscript.echo "Driver supported actions are: " & ar.SupportActions
   wscript.echo "Switch component count is: " & ar.MaxSwitch
End Sub

'******************************************************************************** 
Sub testActions() 
'	Check all the basic driver information first 
	if  ar.Connected = false then  
      ar.Connected = true
	
End Sub
