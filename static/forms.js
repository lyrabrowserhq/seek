/* search box submit-button state, shared by the home/results/images/news
   forms - a dead submit is prevented when the box is empty */
(function () {
    'use strict';

    function bindForm(inputId, btnId, formId, deferInitial) {
        var searchBox = document.getElementById(inputId);
        var searchBtn = document.getElementById(btnId);
        var form = document.getElementById(formId);
        if (!searchBox || !searchBtn || !form) return;
        var updateBtnState = function () {
            var hasValue = searchBox.value.trim().length > 0;
            searchBtn.disabled = !hasValue;
            searchBtn.style.opacity = hasValue ? '1' : '0.4';
            searchBtn.style.cursor = hasValue ? 'pointer' : 'default';
        };
        searchBox.addEventListener('input', updateBtnState);
        if (deferInitial)
            setTimeout(updateBtnState, 100);
        else
            updateBtnState();
        form.addEventListener('submit', function (e) {
            if (!searchBox.value.trim())
                e.preventDefault();
        });
    }

    document.addEventListener('DOMContentLoaded', function () {
        bindForm('home-search', 'home-search-btn', 'home-search-form', true);
        bindForm('results-search', 'results-search-btn', 'results-search-form', false);
        bindForm('images-search', 'images-search-btn', 'images-search-form', false);
        bindForm('news-search', 'news-search-btn', 'news-search-form', false);
    });

    document.addEventListener('click', function (e) {
        document.querySelectorAll('.box-dropdown[open], .engine-overflow[open]').forEach(function (d) {
            if (!d.contains(e.target)) d.removeAttribute('open');
        });
    });
})();
