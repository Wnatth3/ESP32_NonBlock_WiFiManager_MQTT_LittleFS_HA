#ifndef DEBUG_H
#define DEBUG_H

#ifdef _DEBUG_
#define _serialBegin(...) Serial.begin(__VA_ARGS__)
#define _de(...)          Serial.print(__VA_ARGS__)
#define _deln(...)        Serial.println(__VA_ARGS__)
#define _deF(...)         Serial.print(F(__VA_ARGS__))
#define _delnF(...)       Serial.println(F(__VA_ARGS__))  // printing text using the F macro
#define _deVar(label, value) \
    Serial.print(F(label));  \
    Serial.print(value);
#define _deVarln(...)    \
    _deVar(__VA_ARGS__); \
    Serial.println();
#else
#define _serialBegin(...)
#define _de(...)
#define _deln(...)
#define _deF(...)
#define _delnF(...)
#define _deVar(...)
#define _deVarln(...)
#endif

#endif