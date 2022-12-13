static const char canvas_htm[] PROGMEM = "<!-- Author : Andrew Mularoni -->\n"\
"\n"\
"<!DOCTYPE html>\n"\
"<html>\n"\
"<title> ESP32 pattern boi by Andrew Mularoni </title>\n"\
"<script type = \"text/javascript\">\n"\
"    var joystick_thread = null;\n"\
"    var fwd_val = 0;\n"\
"    var turn_val = 0;\n"\
"    var l_blinker = false;\n"\
"    var r_blinker = false;\n"\
"    var horn = false;\n"\
"\n"\
"    function joystickFunction(){\n"\
"        var cmd_obj = new Object();\n"\
"        if(navigator.getGamepads()[0] != null){\n"\
"           var gamepad = navigator.getGamepads()[0];\n"\
"           console.log(\"using gamepad\");\n"\
"           cmd_obj.fwd = gamepad.buttons[7].value - gamepad.buttons[6].value;\n"\
"           cmd_obj.turn = gamepad.axes[0];\n"\
"           cmd_obj.l_turn = gamepad.buttons[4].pressed;\n"\
"           cmd_obj.r_turn = gamepad.buttons[5].pressed;\n"\
"           cmd_obj.horn = gamepad.buttons[2].pressed;\n"\
"        }else{\n"\
"           cmd_obj.fwd = fwd_val;\n"\
"           cmd_obj.turn = turn_val;\n"\
"           cmd_obj.l_turn = l_blinker;\n"\
"           cmd_obj.r_turn = r_blinker;\n"\
"           cmd_obj.horn = horn;\n"\
"         }\n"\
"        let xhr = new XMLHttpRequest();\n"\
"        xhr.open(\"POST\", \"http://\" + location.hostname + \"/cmd\");\n"\
"        xhr.setRequestHeader(\"Accept\", \"application/json\");\n"\
"        xhr.setRequestHeader(\"Content-Type\", \"application/json\");\n"\
"        xhr.send(JSON.stringify(cmd_obj));\n"\
"    };\n"\
"function onloadinit() {\n"\
"   init();\n"\
"}\n"\
"function init() {\n"\
"    joystick_thread = setInterval(joystickFunction, 100);\n"\
"}\n"\
"\n"\
"</script>\n"\
"\n"\
"<body onload=\"onloadinit()\">\n"\
"\n"\
"<center> <h1> Pattern Mini Bot </h1> </center>\n"\
"\n"\
"\n"\
"<table align=center valign=center id=\"canvas7670\">\n"\
"    <tr> \n"\
"        <td valign=\"bottom\">    \n"\
"<img id=\"display_frame\" width=\"1280\" height=\"960\" style=\"cursor:crosshair;border:1px solid #FFFF00;\" src=\"/mjpeg/1\">\n"\
"        </td>\n"\
"\n"\
"</table>\n"\
"<BR><BR>\n"\
"<table width=30% align=center bgcolor=\"#FFFF00\" >\n"\
" <tr align=center style=\"color: #fff; background: black;\"> \n"\
"   <td id=\"constatus\" colspan=3>Not connected</td> \n"\
" </tr>\n"\
"\n"\
" <tr align=center style=\"color: #fff; background: black;\"> \n"\
"   <td><button type=\"button\" onclick=\"l_blinker=true; r_blinker=false; setTimeout(function(){l_blinker=false;}, 5000);\">Left Blinker</button></td> \n"\
"   <td><button type=\"button\" onclick=\"fwd_val = 1.0; turn_val = 0.0;\">Forward</button></td> \n"\
"   <td><button type=\"button\" onclick=\"l_blinker=false; r_blinker=true; setTimeout(function(){r_blinker=false;}, 5000);\">Right Blinker</button></td> \n"\
" </tr>\n"\
"\n"\
" <tr align=center style=\"color: #fff; background: black;\"> \n"\
"   <td><button type=\"button\" onclick=\"fwd_val = 0.0; turn_val = -0.5;\">Left</button></td> \n"\
"   <td><button type=\"button\" onclick=\"fwd_val = 0.0; turn_val = 0.0;\">Stop</button></td> \n"\
"   <td><button type=\"button\" onclick=\"fwd_val = 0.0; turn_val = 0.5;\">Right</button></td> \n"\
" </tr>\n"\
"\n"\
" <tr align=center style=\"color: #fff; background: black;\"> \n"\
"   <td></td> \n"\
"   <td><button type=\"button\" onclick=\"fwd_val = -1.0; turn_val = 0.0;\">Reverse</button></td> \n"\
"   <td><button type=\"button\" onclick=\"horn=true; setTimeout(function(){horn=false;}, 2000);\">Horn</button></td> \n"\
" </tr>\n"\
"         \n"\
"</table> \n"\
"\n"\
"\n"\
"</body>\n"\
"</html>\n"\
"\n";
