; ======================================================================
;  BatteryWatch settings - edit these, then right-click Rainmeter's
;  icon > Refresh all (or refresh both skins in Manage).
; ======================================================================
[Variables]

; Show the centre warning at or below these levels (percent).
MouseThreshold=20
HeadsetThreshold=20

; How often to ask the devices for their battery level, in seconds (minimum 10).
PollSeconds=300

; --- Look ---------------------------------------------------------------
FontFace=Segoe UI

; Top-right corner readout
CornerFontSize=20
CornerColor=255,255,255,110
CornerGap=24

; Centre warning
WarningTitleSize=40
WarningTitleColor=255,50,50,255
ShadowColor=0,0,0,160

; --- Warning wording ----------------------------------------------------
MouseTitle=Charge Mouse!
HeadsetTitle=Charge Headset!
BothTitle=Charge Mouse & Headset!
