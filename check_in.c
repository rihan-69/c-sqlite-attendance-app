#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "sqlite3.h"

// Helper function to decode URL-encoded strings (e.g., "Aarav+Sharma" -> "Aarav Sharma")
void url_decode(char *str) {
    char *p_str = str;
    char hex[3] = {0};
    while (*str) {
        if (*str == '+') {
            *p_str++ = ' ';
        } else if (*str == '%' && str[1] && str[2]) {
            hex[0] = str[1];
            hex[1] = str[2];
            *p_str++ = strtol(hex, NULL, 16);
            str += 2;
        } else {
            *p_str++ = *str;
        }
        str++;
    }
    *p_str = '\0';
}

// Helper function to print a simple HTML response page
void print_html_page(const char *title, const char *message, const char *className) {
    printf("Content-Type: text/html\n\n");
    printf("<!DOCTYPE html><html lang='en'><head><title>%s</title>", title);
    printf("<style>"
           "body { font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', sans-serif; display: flex; justify-content: center; align-items: center; height: 100vh; background-color: #f4f7f9; margin: 0; }"
           ".container { text-align: center; background: #fff; padding: 40px; border-radius: 12px; box-shadow: 0 8px 30px rgba(0,0,0,0.1); }"
           ".message { font-size: 20px; margin-bottom: 25px; }"
           ".success { color: #28a745; }"
           ".error { color: #dc3545; }"
           "a { color: #007bff; text-decoration: none; font-weight: 600; }"
           "</style></head><body><div class='container'>");
    printf("<h1>%s</h1><p class='message %s'>%s</p>", title, className, message);
    printf("<a href='/index.html'>&larr; Back to Portal</a>");
    printf("</div></body></html>");
}

int main() {
    sqlite3 *db;
    sqlite3_stmt *stmt;
    char *err_msg = 0;
    int student_id = -1;

    // 1. GET AND PARSE THE FORM DATA
    char *content_length_str = getenv("CONTENT_LENGTH");
    if (content_length_str == NULL) {
        print_html_page("Error", "No data received from form.", "error");
        return 1;
    }

    int content_length = atoi(content_length_str);
    char *postdata = malloc(content_length + 1);
    fread(postdata, 1, content_length, stdin);
    postdata[content_length] = '\0';

    char name[100] = "", cuid[50] = "", section[100] = "";
    char *p_name = strstr(postdata, "student_name=");
    char *p_cuid = strstr(postdata, "cuid=");
    char *p_section = strstr(postdata, "section_name=");

    if (p_name) sscanf(p_name, "student_name=%[^&]", name);
    if (p_cuid) sscanf(p_cuid, "cuid=%[^&]", cuid);
    if (p_section) sscanf(p_section, "section_name=%[^&]", section);

    url_decode(name);
    url_decode(cuid);
    url_decode(section);
    
    // 2. CONNECT TO THE DATABASE
    if (sqlite3_open("/var/www/html/attendance.db", &db) != SQLITE_OK) {
        print_html_page("Database Error", "Cannot connect to the database.", "error");
        free(postdata);
        return 1;
    }

    // 3. VALIDATE THE STUDENT (CRITICAL STEP)
    // Use a prepared statement to prevent SQL injection
    const char *validation_sql = "SELECT s.student_id FROM students s JOIN sections sec ON s.section_id = sec.section_id "
                                 "WHERE s.name = ? AND s.cuid = ? AND sec.section_name = ?;";

    if (sqlite3_prepare_v2(db, validation_sql, -1, &stmt, 0) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, name, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, cuid, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 3, section, -1, SQLITE_STATIC);

        if (sqlite3_step(stmt) == SQLITE_ROW) {
            student_id = sqlite3_column_int(stmt, 0); // Student is valid, get their ID
        }
        sqlite3_finalize(stmt);
    }

    // 4. LOG ATTENDANCE IF VALID
    if (student_id != -1) {
        // Get current date
        time_t t = time(NULL);
        // Add IST offset (5 hours * 3600 seconds + 30 minutes * 60 seconds)
        t += 19800; 
        struct tm tm = *gmtime(&t);
        char current_date[11];
        strftime(current_date, sizeof(current_date), "%Y-%m-%d", &tm);

        // Insert or update attendance record. ON CONFLICT prevents duplicates for the same day.
        const char *log_sql = "INSERT INTO attendance_records (student_id, attendance_date, status) VALUES (?, ?, 'Present') "
                              "ON CONFLICT(student_id, attendance_date) DO UPDATE SET status='Present';";

        if (sqlite3_prepare_v2(db, log_sql, -1, &stmt, 0) == SQLITE_OK) {
            sqlite3_bind_int(stmt, 1, student_id);
            sqlite3_bind_text(stmt, 2, current_date, -1, SQLITE_STATIC);
            
            if (sqlite3_step(stmt) == SQLITE_DONE) {
                print_html_page("Success", "Your attendance has been marked successfully!", "success");
            } else {
                print_html_page("Error", "Failed to log attendance.", "error");
            }
            sqlite3_finalize(stmt);
        }
    } else {
        // Student was not found in the database
        print_html_page("Validation Failed", "Could not find a student with the provided details. Please check your Name, CU ID, and Section.", "error");
    }

    // 5. CLEAN UP
    sqlite3_close(db);
    free(postdata);

    return 0;
}


