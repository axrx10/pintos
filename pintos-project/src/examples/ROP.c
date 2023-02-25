void function_exploitable()
{
char buf[128];
read(1, buf, 256);
}



int main(int argc, char ** argv)
{
function_exploitable();
printf("\n Hello world");
}
