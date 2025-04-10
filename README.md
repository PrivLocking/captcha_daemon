# redis static

a chaptcha daemon written in pure C code.
it must work with redis database

## it list on 3 http path: 
*  /captcha_png
*  /captcha_auth
*  /captcha_check

##  /captcha_png
A random PNG generator.
It will generated PNG, 
togethere with a 32 bytes(in HEX ASCII) `session cookie` in cookie.


## /captcha_auth
When the user submit the code to server for auth, user this path. 
`Auth cookie` is generated in the block.

## /captcha_check
When the user/client want to visit protected area, use this to to verify the `session cookie` and `auth cookie`.

## Example html: `src/index.html`

An example how to use this chaptcha daemon

## for more details :
Please check my [Blog](https://blog00.jjj123.com/post/2025/04/20250406_210240/).

