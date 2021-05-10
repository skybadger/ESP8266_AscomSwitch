var inString = "2019-7-24 17:56:32.12Z";
var newDate = Date.parse( inString) ;
if( newDate === NaN )
{
   console.log( "oh no" );

}
else
{
   console.log( "new date is:" + newDate ) ;
}
