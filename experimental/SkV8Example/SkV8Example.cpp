/*
 * Copyright 2013 Google Inc.
 *
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 */
#include <v8.h>

using namespace v8;

#include "SkV8Example.h"

#include "gl/GrGLUtil.h"
#include "gl/GrGLDefines.h"
#include "gl/GrGLInterface.h"
#include "SkApplication.h"
#include "SkDraw.h"
#include "SkGpuDevice.h"
#include "SkGraphics.h"


void application_init() {
    SkGraphics::Init();
    SkEvent::Init();
}

void application_term() {
    SkEvent::Term();
    SkGraphics::Term();
}

// Extracts a C string from a V8 Utf8Value.
const char* ToCString(const v8::String::Utf8Value& value) {
    return *value ? *value : "<string conversion failed>";
}

// Slight modification to an original function found in the V8 sample shell.cc.
void reportException(Isolate* isolate, TryCatch* try_catch) {
    HandleScope handleScope(isolate);
    String::Utf8Value exception(try_catch->Exception());
    const char* exception_string = ToCString(exception);
    Handle<Message> message = try_catch->Message();
    if (message.IsEmpty()) {
        // V8 didn't provide any extra information about this error; just
        // print the exception.
        fprintf(stderr, "%s\n", exception_string);
    } else {
        // Print (filename):(line number): (message).
        String::Utf8Value filename(message->GetScriptResourceName());
        const char* filename_string = ToCString(filename);
        int linenum = message->GetLineNumber();
        fprintf(stderr,
                "%s:%i: %s\n", filename_string, linenum, exception_string);
        // Print line of source code.
        String::Utf8Value sourceline(message->GetSourceLine());
        const char* sourceline_string = ToCString(sourceline);
        fprintf(stderr, "%s\n", sourceline_string);
        // Print wavy underline.
        int start = message->GetStartColumn();
        for (int i = 0; i < start; i++) {
            fprintf(stderr, " ");
        }
        int end = message->GetEndColumn();
        for (int i = start; i < end; i++) {
            fprintf(stderr, "^");
        }
        fprintf(stderr, "\n");
        String::Utf8Value stack_trace(try_catch->StackTrace());
        if (stack_trace.length() > 0) {
            const char* stack_trace_string = ToCString(stack_trace);
            fprintf(stderr, "%s\n", stack_trace_string);
        }
    }
}

SkV8ExampleWindow::SkV8ExampleWindow(void* hwnd, JsCanvas* canvas)
    : INHERITED(hwnd)
    , fJsCanvas(canvas)
{
    fRotationAngle = SkIntToScalar(0);
    this->setConfig(SkBitmap::kARGB_8888_Config);
    this->setVisibleP(true);
    this->setClipToBounds(false);
}

JsCanvas* JsCanvas::unwrap(Handle<Object> obj) {
    Handle<External> field = Handle<External>::Cast(obj->GetInternalField(0));
    void* ptr = field->Value();
    return static_cast<JsCanvas*>(ptr);
}

void JsCanvas::inval(const v8::FunctionCallbackInfo<Value>& args) {
    unwrap(args.This())->fWindow->inval(NULL);
}

void JsCanvas::drawRect(const v8::FunctionCallbackInfo<Value>& args) {
    SkCanvas* canvas = unwrap(args.This())->fCanvas;

    canvas->drawColor(SK_ColorWHITE);

    // Draw a rectangle with red paint.
    SkPaint paint;
    paint.setColor(SK_ColorRED);
    SkRect rect = {
        SkIntToScalar(10), SkIntToScalar(10),
        SkIntToScalar(128), SkIntToScalar(128)
    };
    canvas->drawRect(rect, paint);
}

Persistent<ObjectTemplate> JsCanvas::fCanvasTemplate;

Handle<ObjectTemplate> JsCanvas::makeCanvasTemplate() {
    EscapableHandleScope handleScope(fIsolate);

    Local<ObjectTemplate> result = ObjectTemplate::New();

    // Add a field to store the pointer to a JsCanvas instance.
    result->SetInternalFieldCount(1);

    // Add accessors for each of the fields of the canvas object.
    result->Set(
            String::NewFromUtf8(
                    fIsolate, "drawRect", String::kInternalizedString),
            FunctionTemplate::New(drawRect));
    result->Set(
            String::NewFromUtf8(
                    fIsolate, "inval", String::kInternalizedString),
            FunctionTemplate::New(inval));

    // Return the result through the current handle scope.
    return handleScope.Escape(result);
}


// Wraps 'this' in a Javascript object.
Handle<Object> JsCanvas::wrap() {
    // Handle scope for temporary handles.
    EscapableHandleScope handleScope(fIsolate);

    // Fetch the template for creating JavaScript JsCanvas wrappers.
    // It only has to be created once, which we do on demand.
    if (fCanvasTemplate.IsEmpty()) {
        Handle<ObjectTemplate> raw_template = this->makeCanvasTemplate();
        fCanvasTemplate.Reset(fIsolate, raw_template);
    }
    Handle<ObjectTemplate> templ =
            Local<ObjectTemplate>::New(fIsolate, fCanvasTemplate);

    // Create an empty JsCanvas wrapper.
    Local<Object> result = templ->NewInstance();

    // Wrap the raw C++ pointer in an External so it can be referenced
    // from within JavaScript.
    Handle<External> canvasPtr = External::New(fIsolate, this);

    // Store the canvas pointer in the JavaScript wrapper.
    result->SetInternalField(0, canvasPtr);

    // Return the result through the current handle scope.  Since each
    // of these handles will go away when the handle scope is deleted
    // we need to call Close to let one, the result, escape into the
    // outer handle scope.
    return handleScope.Escape(result);
}

void JsCanvas::onDraw(SkCanvas* canvas, SkOSWindow* window) {
    // Record canvas and window in this.
    fCanvas = canvas;
    fWindow = window;

    // Create a handle scope to keep the temporary object references.
    HandleScope handleScope(fIsolate);

    // Create a local context from our global context.
    Local<Context> context = Local<Context>::New(fIsolate, fContext);

    // Enter the context so all the remaining operations take place there.
    Context::Scope contextScope(context);

    // Wrap the C++ this pointer in a JavaScript wrapper.
    Handle<Object> canvasObj = this->wrap();

    // Set up an exception handler before calling the Process function.
    TryCatch tryCatch;

    // Invoke the process function, giving the global object as 'this'
    // and one argument, this JsCanvas.
    const int argc = 1;
    Handle<Value> argv[argc] = { canvasObj };
    Local<Function> onDraw =
            Local<Function>::New(fIsolate, fOnDraw);
    Handle<Value> result = onDraw->Call(context->Global(), argc, argv);

    // Handle any exceptions or output.
    if (result.IsEmpty()) {
        SkASSERT(tryCatch.HasCaught());
        // Print errors that happened during execution.
        reportException(fIsolate, &tryCatch);
    } else {
        SkASSERT(!tryCatch.HasCaught());
        if (!result->IsUndefined()) {
            // If all went well and the result wasn't undefined then print
            // the returned value.
            String::Utf8Value str(result);
            const char* cstr = ToCString(str);
            printf("%s\n", cstr);
        }
    }
}

void SkV8ExampleWindow::onDraw(SkCanvas* canvas) {

    canvas->save();

    fRotationAngle += SkDoubleToScalar(0.2);
    if (fRotationAngle > SkDoubleToScalar(360.0)) {
        fRotationAngle -= SkDoubleToScalar(360.0);
    }

    canvas->rotate(fRotationAngle);

    // Now jump into JS and call the onDraw(canvas) method defined there.
    fJsCanvas->onDraw(canvas, this);

    canvas->restore();

    INHERITED::onDraw(canvas);
}


#ifdef SK_BUILD_FOR_WIN
void SkV8ExampleWindow::onHandleInval(const SkIRect& rect) {
    RECT winRect;
    winRect.top = rect.top();
    winRect.bottom = rect.bottom();
    winRect.right = rect.right();
    winRect.left = rect.left();
    InvalidateRect((HWND)this->getHWND(), &winRect, false);
}
#endif

// Creates a new execution environment containing the built-in
// function draw().
Handle<Context> createRootContext(Isolate* isolate) {
  // Create a template for the global object.
  Handle<ObjectTemplate> global = ObjectTemplate::New();

  // This is where we would inject any globals into the root Context
  // using global->Set(...)

  return Context::New(isolate, NULL, global);
}


// Parse and run script. Then fetch out the onDraw function from the global
// object.
bool JsCanvas::initialize(const char script[]) {

    // Create a stack-allocated handle scope.
    HandleScope handleScope(fIsolate);

    printf("Before create context\n");

    // Create a new context.
    Handle<Context> context = createRootContext(fIsolate);

    // Enter the scope so all operations take place in the scope.
    Context::Scope contextScope(context);

    v8::TryCatch try_catch;

    // Compile the source code.
    Handle<String> source = String::NewFromUtf8(fIsolate, script);
    printf("Before Compile\n");
    Handle<Script> compiled_script = Script::Compile(source);
    printf("After Compile\n");

    if (compiled_script.IsEmpty()) {
        // Print errors that happened during compilation.
        reportException(fIsolate, &try_catch);
        return false;
    }
    printf("After Exception.\n");

    // Try running it now to create the onDraw function.
    Handle<Value> result = compiled_script->Run();

    // Handle any exceptions or output.
    if (result.IsEmpty()) {
        SkASSERT(try_catch.HasCaught());
        // Print errors that happened during execution.
        reportException(fIsolate, &try_catch);
        return false;
    } else {
        SkASSERT(!try_catch.HasCaught());
        if (!result->IsUndefined()) {
            // If all went well and the result wasn't undefined then print
            // the returned value.
            String::Utf8Value str(result);
            const char* cstr = ToCString(str);
            printf("%s\n", cstr);
            return false;
        }
    }

    Handle<String> fn_name = String::NewFromUtf8(fIsolate, "onDraw");
    Handle<Value> fn_val = context->Global()->Get(fn_name);

    if (!fn_val->IsFunction()) {
        return false;
    }

    // It is a function; cast it to a Function.
    Handle<Function> fn_fun = Handle<Function>::Cast(fn_val);

    // Store the function in a Persistent handle, since we also want that to
    // remain after this call returns.
    fOnDraw.Reset(fIsolate, fn_fun);

    // Also make the context persistent.
    fContext.Reset(fIsolate, context);
    return true;
}

SkOSWindow* create_sk_window(void* hwnd, int argc, char** argv) {
    printf("Started\n");

    // Get the default Isolate created at startup.
    Isolate* isolate = Isolate::GetCurrent();

    JsCanvas* jsCanvas = new JsCanvas(isolate);
    const char* script = "function onDraw(canvas){"
            "canvas.drawRect();"
            "canvas.inval();"
            "};";
    if (!jsCanvas->initialize(script)) {
        printf("Failed to initialize.\n");
        exit(1);
    }

    return new SkV8ExampleWindow(hwnd, jsCanvas);
}
