<% do_pagehead("join.titl"); %>
		<script type="text/javascript">
		//<![CDATA[

var SSID = "<% nvram_get("wl_ssid"); %>";

function to_send(url)
{
	opener.focus();
	opener.location.href = url;
}
      
		//]]>
		</script>
    </head>

    <body onunload="to_send('Wireless_Basic.asp')">
      <div class="message">
         <div>
            <form>
            	<script type="text/javascript">
            	//<![CDATA[
            	document.write(join.mess1 + "&nbsp;" + SSID + "<br/>");
            	document.write("<input class=\"button\" type=\"button\" name=\"action\" value=\"" + sbutton.continu + "\" onclick=\"self.close();\" />");
            	//]]>
            	</script>
            </form>
         </div>
      </div>
   </body>
</html>